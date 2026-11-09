#include "RuntimeAnimatorSystem.hpp"
#include "AnimationLayerStack.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "asset/AssetRef.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <system_error>

// VK-1656 edit-mode preview. Everything here runs on the main thread: the start/stop/status API
// in the MCP drain (render thread idle), updateEditModePreview() in FramePreparationSystem's
// prepareMeshes. The render thread only reads the animators' palettes, after Render.

namespace animation
{
    namespace
    {
        // Above this a single frame's advance could spin AnimatorStateMachine's loop wrap.
        constexpr float maxPreviewSpeed = 100.0f;

        services::AnimationEditPreviewResult previewError(std::string error)
        {
            services::AnimationEditPreviewResult result;
            result.error = std::move(error);
            return result;
        }

        std::string stateNames(const animator::AnimatorGraph& graph)
        {
            std::string names;
            for (const auto& state : graph.states)
            {
                names += names.empty() ? "" : ", ";
                names += state.name;
            }
            return names.empty() ? "(none)" : names;
        }

        std::string parameterNames(const animator::AnimatorGraph& graph)
        {
            std::string names;
            for (const auto& parameter : graph.parameters)
            {
                names += names.empty() ? "" : ", ";
                names += parameter.name;
            }
            return names.empty() ? "(none)" : names;
        }

        // Converts a requested value to the parameter's type: Float and Int take either number (a
        // float rounds to the nearest int), Bool and Trigger take only a bool. Returns the error,
        // empty on success.
        std::string convertParameterValue(const animator::AnimatorParameter& parameter,
                                          const animator::AnimatorParameterValue& value,
                                          animator::AnimatorParameterValue& converted)
        {
            const std::string typeName = animator::parameterTypeToString(parameter.type);
            const bool isBool = std::holds_alternative<bool>(value);
            if (std::holds_alternative<float>(value) && !std::isfinite(std::get<float>(value)))
                return "parameter '" + parameter.name + "' must be a finite number";

            switch (parameter.type)
            {
            case animator::AnimatorParameterType::Float:
                if (isBool)
                    return "parameter '" + parameter.name + "' is a Float and takes a number";
                if (std::holds_alternative<float>(value))
                    converted.emplace<float>(std::get<float>(value));
                else
                    converted.emplace<float>(static_cast<float>(std::get<int32_t>(value)));
                return {};

            case animator::AnimatorParameterType::Int:
                if (isBool)
                    return "parameter '" + parameter.name + "' is an Int and takes a number";
                if (std::holds_alternative<int32_t>(value))
                {
                    converted.emplace<int32_t>(std::get<int32_t>(value));
                }
                else
                {
                    const double rounded = std::round(static_cast<double>(std::get<float>(value)));
                    if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
                        rounded > static_cast<double>(std::numeric_limits<int32_t>::max()))
                        return "parameter '" + parameter.name + "' is out of the Int range";
                    converted.emplace<int32_t>(static_cast<int32_t>(rounded));
                }
                return {};

            case animator::AnimatorParameterType::Bool:
            case animator::AnimatorParameterType::Trigger:
                if (!isBool)
                    return "parameter '" + parameter.name + "' is a " + typeName + " and takes true or false";
                converted.emplace<bool>(std::get<bool>(value));
                return {};
            }
            return "parameter '" + parameter.name + "' has an unknown type";
        }
    }

    services::AnimationEditPreviewResult RuntimeAnimatorSystem::startEditPreview(
        entt::entity entity, const services::AnimationEditPreviewRequest& request)
    {
        if (!std::isfinite(request.normalizedTime))
            return previewError("normalizedTime must be a finite number");
        if (!std::isfinite(request.speed) || request.speed < 0.0f || request.speed > maxPreviewSpeed)
            return previewError("speed must be between 0 and 100");

        // Queued reloads first: a save earlier in this drain must not end the new session at the
        // next sync, and the session must be built from the saved data.
        applyPendingRequests();

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.valid(entity))
            return previewError("entity does not exist");

        const auto* meshComp = registry.try_get<components::MeshComponent>(entity);
        if (!meshComp || !meshComp->meshRef.isValid())
            return previewError("entity has no mesh");
        if (!meshComp->animatorRef.isValid())
            return previewError("entity has no Animator");

        // Same per-entity step as syncWithRegistry(): an animator built from other assets than
        // the component names now would be rebuilt (and this session ended) at the next sync.
        syncEntityAnimator(entity, *meshComp);
        AnimationLayerStack* animatorStack = getLayerStack(entity);
        if (!animatorStack || !animatorStack->isInitialized())
            return previewError("animator could not be initialized (does the mesh have a skeleton?)");
        // The renderer uploads a pose only for an animator whose base layer is a state machine; a direct-clip
        // base layer never plays (validateAnimator reports it), so no preview on it would ever show.
        if (!animatorStack->getBaseStateMachine())
            return previewError("the animator's base layer plays a direct clip, which the runtime never plays (the "
                                "mesh stays in its bind pose); make layer 0 a state machine first");

        // The animator's own skeleton (same cache key it was initialized with).
        const resource::SkeletonData* skeleton = dataCache.loadSkeleton(meshComp->meshRef.resolve());
        if (!skeleton)
            return previewError("the mesh has no skeleton");

        auto data = std::make_unique<animator::AnimatorData>();
        data->name = "Preview";
        animator::AnimatorGraph& graph = data->graph;
        asset::AssetGUID sourceAnimator;

        if (request.source == services::AnimationPreviewSource::State)
        {
            // The data the animator runs (getAnimatorData() points into the cached entry).
            const animator::AnimatorData* animatorData = animatorStack->getAnimatorData();
            if (!animatorData)
                return previewError("the entity's animator has no data");

            // Layer 0 is a state machine here (a direct-clip base layer was refused above).
            const animator::AnimatorGraph* baseGraph =
                animatorData->layers.empty() ? &animatorData->graph : &animatorData->layers[0].graph;

            const animator::AnimatorState* state = baseGraph->findStateByName(request.state);
            if (!state)
                return previewError("state '" + request.state + "' not found; states: " + stateNames(*baseGraph));
            if (!state->blendTree.has_value() && !state->animationRef.isValid())
                return previewError("state '" + request.state + "' has no clip");

            animator::AnimatorState& previewState = graph.states.emplace_back(*state);
            previewState.id = 1;
            previewState.playbackSpeed *= request.speed;

            // The parameters the animator runs with: the union over its state-machine layers,
            // first definition wins (AnimationLayerStack::initializeSharedParameters).
            if (animatorData->layers.empty())
            {
                graph.parameters = baseGraph->parameters;
            }
            else
            {
                for (const auto& layer : animatorData->layers)
                {
                    if (layer.sourceMode != animator::LayerSourceMode::StateMachine)
                        continue;
                    for (const auto& parameter : layer.graph.parameters)
                    {
                        if (!graph.findParameter(parameter.name))
                            graph.parameters.push_back(parameter);
                    }
                }
            }

            for (const auto& requested : request.parameters)
            {
                const std::string& name = requested.first;
                auto parameter = std::find_if(graph.parameters.begin(), graph.parameters.end(),
                    [&name](const animator::AnimatorParameter& candidate) { return candidate.name == name; });
                if (parameter == graph.parameters.end())
                    return previewError("parameter '" + name + "' not found; parameters: " + parameterNames(graph));

                animator::AnimatorParameterValue converted;
                std::string error = convertParameterValue(*parameter, requested.second, converted);
                if (!error.empty())
                    return previewError(std::move(error));
                parameter->defaultValue = converted;
            }

            const auto sourcesIt = animatorSources.find(entity);
            if (sourcesIt != animatorSources.end())
                sourceAnimator = sourcesIt->second.animator;
        }
        else
        {
            if (request.clipPath.empty())
                return previewError("no clip given");
            // AssetRef::fromPath registers any path with a known extension, existing or not.
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(std::filesystem::path(request.clipPath), fileError))
                return previewError("clip '" + request.clipPath + "' does not exist");

            const asset::AssetRef clipRef = asset::AssetRef::fromPath(request.clipPath);
            if (!clipRef.isValid())
                return previewError("clip '" + request.clipPath + "' is not a known asset");
            // Through the key the state machine loads with (the database's spelling of the path).
            if (!dataCache.loadAnimation(clipRef.resolve()))
                return previewError("clip '" + request.clipPath + "' could not be loaded");

            animator::AnimatorState& previewState = graph.states.emplace_back();
            previewState.id = 1;
            previewState.name = "Preview";
            previewState.animationRef = clipRef;
            previewState.playbackSpeed = request.speed;
            previewState.loop = request.loop;
        }
        graph.defaultStateId = 1;
        graph.nextStateId = 2;

        EditPreviewSession session;
        session.request = request;
        session.request.normalizedTime = std::clamp(request.normalizedTime, 0.0f, 1.0f);
        session.data = std::move(data);
        session.retarget = makeRetargetContext(entity, skeleton);
        session.stack = std::make_unique<AnimationLayerStack>();
        session.stack->initialize(*session.data, skeleton, [this](const std::string& path) -> const resource::AnimationData*
        {
            return dataCache.loadAnimation(path);
        }, session.retarget.get());

        // Pose like the animator does: with root motion on, the evaluator pins the root bone.
        const AnimatorStateMachine* animatorBase = animatorStack->getBaseStateMachine();
        session.stack->setRootMotionEnabled(animatorBase && animatorBase->isRootMotionEnabled());
        // The seek re-evaluates the pose, so a paused preview holds the pose at that time and a
        // playing one starts from it.
        session.stack->setNormalizedTime(session.request.normalizedTime);
        if (session.stack->getBoneMatrices().empty())
            return previewError("the preview produced no pose (is the clip compatible with this skeleton?)");

        session.animator = sourceAnimator;
        session.mesh = meshComp->meshRef.getGUID();
        session.durationSeconds = session.stack->getCurrentStateDuration();

        services::AnimationEditPreviewResult result;
        result.success = true;
        result.durationSeconds = session.durationSeconds;
        result.retargeted = session.retarget != nullptr;

        // Erase first: a move-assignment would free the old session's data before its stack.
        editPreviews.erase(entity);
        editPreviews.emplace(entity, std::move(session));
        editPreviewDirty = true;
        return result;
    }

    bool RuntimeAnimatorSystem::stopEditPreview(entt::entity entity)
    {
        if (editPreviews.erase(entity) == 0)
            return false;

        releasePreviewPose(entity);
        editPreviewDirty = true;
        return true;
    }

    bool RuntimeAnimatorSystem::stopAllEditPreviews()
    {
        if (editPreviews.empty())
            return false;

        for (const auto& [entity, session] : editPreviews)
            releasePreviewPose(entity);
        editPreviews.clear();
        editPreviewDirty = true;
        return true;
    }

    std::optional<services::AnimationEditPreviewStatus> RuntimeAnimatorSystem::getEditPreviewStatus(entt::entity entity) const
    {
        auto it = editPreviews.find(entity);
        if (it == editPreviews.end())
            return std::nullopt;

        const EditPreviewSession& session = it->second;
        services::AnimationEditPreviewStatus status;
        status.source = session.request.source;
        status.state = session.request.state;
        status.clipPath = session.request.clipPath;
        status.normalizedTime = session.stack->getNormalizedTime();
        status.durationSeconds = session.durationSeconds;
        status.playing = session.request.playing;
        return status;
    }

    void RuntimeAnimatorSystem::releasePreviewPose(entt::entity entity)
    {
        // The animator's palette still holds the session's pose. Clearing it makes the next
        // evaluation (rest pose in edit mode, update() in Play) rebuild it from the animator
        // alone: a base layer with nothing to evaluate leaves an empty palette (bind pose)
        // instead of the last preview frame.
        if (AnimationLayerStack* stack = getLayerStack(entity))
            stack->getMutableBoneMatrices().clear();
    }

    void RuntimeAnimatorSystem::pruneEditPreviews()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> ended;
        for (const auto& [entity, session] : editPreviews)
        {
            const auto* meshComp = registry.valid(entity) ? registry.try_get<components::MeshComponent>(entity) : nullptr;
            bool keep = meshComp && hasAnimator(entity) && meshComp->meshRef.getGUID() == session.mesh;
            if (keep && session.request.source == services::AnimationPreviewSource::State)
            {
                // A state preview is a copy of the animator's state: it ends with that animator.
                const auto sourcesIt = animatorSources.find(entity);
                keep = sourcesIt != animatorSources.end() && sourcesIt->second.animator == session.animator;
            }
            if (!keep)
                ended.push_back(entity);
        }

        for (auto entity : ended)
            stopEditPreview(entity);
    }

    void RuntimeAnimatorSystem::updateEditModePreview(float deltaTime)
    {
        applyPendingRequests();
        // Consumed up front: a markEditPreviewDirty() from another thread during this pass stays set for the
        // next frame instead of being cleared after it.
        if (!editPreviewDirty.exchange(false) && editPreviews.empty())
            return;

        processPendingStreamingInits();
        syncWithRegistry();
        pruneEditPreviews();

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> misfit;
        for (auto& [entity, anim] : animators)
        {
            if (!anim || !anim->isInitialized())
                continue;

            auto previewIt = editPreviews.find(entity);
            if (previewIt == editPreviews.end())
            {
                anim->evaluateRestPose();
            }
            else
            {
                // Paused sessions keep the pose they were seeked to.
                EditPreviewSession& session = previewIt->second;
                if (session.request.playing)
                    session.stack->update(deltaTime);

                // Same skeleton as the animator, so the pose fits its palette (empty = the
                // animator has no pose of its own, e.g. a default state without a clip).
                const std::vector<glm::mat4>& pose = session.stack->getBoneMatrices();
                std::vector<glm::mat4>& palette = anim->getMutableBoneMatrices();
                if (!pose.empty() && (palette.empty() || palette.size() == pose.size()))
                {
                    palette = pose;
                }
                else
                {
                    misfit.push_back(entity);
                    palette.clear(); // see releasePreviewPose()
                    anim->evaluateRestPose();
                }
            }
            applyIKPostProcess(entity, anim.get(), registry);
        }

        for (auto entity : misfit)
        {
            vfLogWarning("[RuntimeAnimatorSystem] Entity {} edit preview stopped: its pose does not fit the animator",
                         static_cast<uint32_t>(entity));
            editPreviews.erase(entity); // its palette was already rebuilt from the animator above
        }
    }
}
