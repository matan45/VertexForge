#include "AnimatorSystemController.hpp"
#include "../animation/RuntimeAnimatorSystem.hpp"
#include "../animation/AnimationLayerStack.hpp"
#include "../animation/AnimatorStateMachine.hpp"
#include "../../services/events/project/ResourceEvents.hpp"
#include "../../services/events/scene/ComponentMediaEvents.hpp"
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/physics/SocketEvents.hpp"
#include "../../services/events/physics/IKEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace controllers
{
    namespace
    {
        bool isAnimatorAssetPath(const std::string& path)
        {
            constexpr std::string_view extension = ".vfanimator";
            if (path.size() < extension.size())
                return false;
            const std::string_view tail = std::string_view(path).substr(path.size() - extension.size());
            return std::equal(extension.begin(), extension.end(), tail.begin(), tail.end(),
                [](char expected, char actual)
                {
                    return expected == static_cast<char>(std::tolower(static_cast<unsigned char>(actual)));
                });
        }
    }

    void AnimatorSystemController::init()
    {
        animation::RuntimeAnimatorSystem::instance().initialize();

        // Handlers only enqueue: publishers can be worker threads (MeshDataChanged from the
        // Scripts task) or run after the frame graph (a fatal-script Stop). RuntimeAnimatorSystem
        // applies the requests on the main thread in prepareMeshes.
        auto& dispatcher = events::EventDispatcher::instance();
        forwarders.emplace_back(dispatcher.subscribe<events::resource::AssetSavedNotification>(
            [](const events::resource::AssetSavedNotification& notification)
            {
                if (isAnimatorAssetPath(notification.filePath))
                    animation::RuntimeAnimatorSystem::instance().requestAnimatorReload(notification.filePath);
            }));
        forwarders.emplace_back(dispatcher.subscribe<events::socket::SocketDataSavedNotification>(
            [](const events::socket::SocketDataSavedNotification& notification)
            {
                animation::RuntimeAnimatorSystem::instance().requestMeshReload(notification.meshPath);
            }));
        forwarders.emplace_back(dispatcher.subscribe<events::ik::IKChainDataSavedNotification>(
            [](const events::ik::IKChainDataSavedNotification& notification)
            {
                animation::RuntimeAnimatorSystem::instance().requestMeshReload(notification.meshPath);
            }));
        forwarders.emplace_back(dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [](const events::scene::MeshDataChangedNotification&)
            {
                animation::RuntimeAnimatorSystem::instance().markEditPreviewDirty();
            }));
        forwarders.emplace_back(dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [](const events::editor::EditorModeChangedNotification&)
            {
                animation::RuntimeAnimatorSystem::instance().requestModeTransition();
            }));
    }

    void AnimatorSystemController::cleanUp()
    {
        forwarders.clear();
        animation::RuntimeAnimatorSystem::instance().shutdown();
    }

    void AnimatorSystemController::setFloat(entt::entity entity, const std::string& paramName, float value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setFloat(paramName, value);
    }

    void AnimatorSystemController::setInt(entt::entity entity, const std::string& paramName, int32_t value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setInt(paramName, value);
    }

    void AnimatorSystemController::setBool(entt::entity entity, const std::string& paramName, bool value)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setBool(paramName, value);
    }

    void AnimatorSystemController::setTrigger(entt::entity entity, const std::string& paramName)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->setTrigger(paramName);
    }

    float AnimatorSystemController::getFloat(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getFloat(paramName);
        return 0.0f;
    }

    int32_t AnimatorSystemController::getInt(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getInt(paramName);
        return 0;
    }

    bool AnimatorSystemController::getBool(entt::entity entity, const std::string& paramName) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getBool(paramName);
        return false;
    }

    void AnimatorSystemController::play(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->play();
    }

    void AnimatorSystemController::pause(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->pause();
    }

    void AnimatorSystemController::stop(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->stop();
    }

    void AnimatorSystemController::reset(entt::entity entity)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            animator->reset();
    }

    bool AnimatorSystemController::isPlaying(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->isPlaying();
        return false;
    }

    bool AnimatorSystemController::isBlending(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->isBlending();
        return false;
    }

    std::string AnimatorSystemController::getCurrentState(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator && animator->isInitialized())
        {
            auto* currentState = animator->getCurrentAnimatorState();
            if (currentState)
                return currentState->name;
        }
        return "";
    }

    float AnimatorSystemController::getNormalizedTime(entt::entity entity) const
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
            return animator->getNormalizedStateTime();
        return 0.0f;
    }

    bool AnimatorSystemController::hasAnimator(entt::entity entity) const
    {
        return animation::RuntimeAnimatorSystem::instance().hasAnimator(entity);
    }

    bool AnimatorSystemController::forceTransitionTo(entt::entity entity, const std::string& stateName,
                                                     float blendDuration)
    {
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
        {
            animator->forceTransitionTo(stateName, blendDuration);
            return true;
        }
        return false;
    }

    void AnimatorSystemController::setRootMotion(entt::entity entity, bool enabled)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
        {
            registry.get<components::AnimatorComponent>(entity).applyRootMotion = enabled;
        }
        if (registry.valid(entity) && registry.all_of<components::MeshComponent>(entity))
        {
            registry.get<components::MeshComponent>(entity).applyRootMotion = enabled;
        }
        auto* animator = animation::RuntimeAnimatorSystem::instance().getAnimator(entity);
        if (animator)
        {
            animator->setRootMotionEnabled(enabled);
        }
    }

    bool AnimatorSystemController::getRootMotion(entt::entity entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (registry.valid(entity) && registry.all_of<components::AnimatorComponent>(entity))
        {
            return registry.get<components::AnimatorComponent>(entity).applyRootMotion;
        }
        return false;
    }

    services::AnimatorRuntimeDebugData AnimatorSystemController::getDebugData(entt::entity entity, uint32_t layerIndex) const
    {
        services::AnimatorRuntimeDebugData result;

        auto* stack = animation::RuntimeAnimatorSystem::instance().getLayerStack(entity);
        if (!stack)
            return result;

        const auto* sm = stack->getLayerStateMachine(layerIndex);
        if (!sm || !sm->isInitialized())
            return result;

        result.isValid = true;

        // State machine state
        const auto& machineState = sm->getMachineState();
        result.currentStateId = machineState.currentStateId;
        result.previousStateId = machineState.previousStateId;
        result.stateTime = machineState.stateTime;
        result.isBlending = machineState.isBlending;
        result.blendProgress = (machineState.blendDuration > 0.0f)
            ? (machineState.blendElapsed / machineState.blendDuration)
            : 0.0f;

        // Find active transition
        const auto* graph = sm->getActiveGraph();
        if (graph && machineState.isBlending)
        {
            for (const auto& transition : graph->transitions)
            {
                if (transition.sourceStateId == machineState.previousStateId &&
                    transition.targetStateId == machineState.currentStateId)
                {
                    result.activeTransitionId = transition.id;
                    break;
                }
            }
        }

        // Current parameter values
        const auto& params = stack->getSharedParameters();
        if (graph)
        {
            for (const auto& paramDef : graph->parameters)
            {
                services::AnimatorParameterDebugInfo info;
                info.name = paramDef.name;
                info.type = paramDef.type;

                switch (paramDef.type)
                {
                case animator::AnimatorParameterType::Float:
                    info.currentValue = params.getFloat(paramDef.name);
                    break;
                case animator::AnimatorParameterType::Int:
                    info.currentValue = params.getInt(paramDef.name);
                    break;
                case animator::AnimatorParameterType::Bool:
                case animator::AnimatorParameterType::Trigger:
                    info.currentValue = params.getBool(paramDef.name);
                    break;
                }
                result.parameters.push_back(info);
            }

            // Evaluate conditions for outgoing transitions from current state
            for (const auto& transition : graph->transitions)
            {
                if (transition.sourceStateId != machineState.currentStateId &&
                    transition.sourceStateId != 0) // 0 = Any State
                    continue;

                for (const auto& condition : transition.conditions)
                {
                    services::AnimatorConditionEval eval;
                    eval.transitionId = transition.id;
                    eval.parameterName = condition.parameterName;
                    eval.op = condition.op;
                    eval.threshold = condition.value;

                    eval.currentValue = params.getFloat(condition.parameterName);
                    // Use actual parameter type
                    for (const auto& paramDef : graph->parameters)
                    {
                        if (paramDef.name == condition.parameterName)
                        {
                            switch (paramDef.type)
                            {
                            case animator::AnimatorParameterType::Float:
                                eval.currentValue = params.getFloat(condition.parameterName);
                                break;
                            case animator::AnimatorParameterType::Int:
                                eval.currentValue = params.getInt(condition.parameterName);
                                break;
                            case animator::AnimatorParameterType::Bool:
                            case animator::AnimatorParameterType::Trigger:
                                eval.currentValue = params.getBool(condition.parameterName);
                                break;
                            }
                            break;
                        }
                    }

                    eval.result = animator::evaluateCondition(condition, params);
                    result.conditionResults.push_back(eval);
                }
            }
        }

        return result;
    }

    void AnimatorSystemController::setLayerWeight(entt::entity entity, uint32_t layerIndex, float weight)
    {
        auto* stack = animation::RuntimeAnimatorSystem::instance().getLayerStack(entity);
        if (stack)
            stack->setLayerWeight(layerIndex, weight);
    }

    float AnimatorSystemController::getLayerWeight(entt::entity entity, uint32_t layerIndex) const
    {
        auto* stack = animation::RuntimeAnimatorSystem::instance().getLayerStack(entity);
        if (stack)
            return stack->getLayerWeight(layerIndex);
        return 0.0f;
    }

    uint32_t AnimatorSystemController::getLayerCount(entt::entity entity) const
    {
        auto* stack = animation::RuntimeAnimatorSystem::instance().getLayerStack(entity);
        if (stack)
            return stack->getLayerCount();
        return 0;
    }

    std::string AnimatorSystemController::getLayerName(entt::entity entity, uint32_t layerIndex) const
    {
        auto* stack = animation::RuntimeAnimatorSystem::instance().getLayerStack(entity);
        if (stack)
            return stack->getLayerName(layerIndex);
        return "";
    }

    services::AnimationEditPreviewResult AnimatorSystemController::startEditPreview(
        entt::entity entity, const services::AnimationEditPreviewRequest& request)
    {
        return animation::RuntimeAnimatorSystem::instance().startEditPreview(entity, request);
    }

    bool AnimatorSystemController::stopEditPreview(entt::entity entity)
    {
        return animation::RuntimeAnimatorSystem::instance().stopEditPreview(entity);
    }

    bool AnimatorSystemController::stopAllEditPreviews()
    {
        return animation::RuntimeAnimatorSystem::instance().stopAllEditPreviews();
    }

    std::optional<services::AnimationEditPreviewStatus> AnimatorSystemController::getEditPreviewStatus(entt::entity entity) const
    {
        return animation::RuntimeAnimatorSystem::instance().getEditPreviewStatus(entity);
    }
}
