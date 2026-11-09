#include "print/Log.hpp"
#include "VFXEditPreviewHandler.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/vfx/VFXEditPreviewEvents.hpp"
#include "../../events/vfx/VFXRuntimeEvents.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../data/VFXOverrideApplier.hpp"
#include "asset/AssetDatabase.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <vfx/VFXParamOverrideJson.hpp>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>

namespace services
{
    namespace
    {
        // Same canonical form as the combo cache keys (VFXSequenceRuntimeServiceImpl): an AssetSaved
        // path and a vfxRef.resolve() path name one file only after both are resolved + normalized.
        std::string assetKey(const std::string& path)
        {
            if (path.empty())
                return {};

            std::string normalized = asset::AssetDatabase::instance().resolveAssetPath(path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            std::filesystem::path p(normalized);
            normalized = p.lexically_normal().string();
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            while (!normalized.empty() && normalized.back() == '/')
                normalized.pop_back();

#ifdef _WIN32
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return normalized;
        }

        bool isPlayMode()
        {
            try
            {
                return ::events::EventDispatcher::instance().query(::events::editor::IsPlayModeQuery{});
            }
            catch (const std::exception&)
            {
                return false; // no editor mode service (headless): there is no Play mode
            }
        }

        // Apply is cumulative (assignments), so dropping an override needs a fresh instance.
        bool overrideRemoved(const std::vector<vfx::VFXParamOverride>& applied,
                             const std::vector<vfx::VFXParamOverride>& current)
        {
            for (const auto& appliedOverride : applied)
            {
                const bool kept = std::any_of(current.begin(), current.end(),
                    [&appliedOverride](const vfx::VFXParamOverride& o) { return o.name == appliedOverride.name; });
                if (!kept)
                    return true;
            }
            return false;
        }

        void destroyPreviewInstance(VFXInstanceId instanceId)
        {
            events::vfxruntime::DestroyVFXInstanceCommand destroyCmd;
            destroyCmd.instanceId = instanceId;
            ::events::EventDispatcher::instance().execute(destroyCmd);
        }

        // create -> apply overrides -> play, like VFXPlayModeHandler. 0 when the renderer refused
        // (file missing, or the effect is disabled at the active quality tier).
        VFXInstanceId createPreviewInstance(entt::entity entity, const components::VFXComponent& comp,
                                            const std::string& path, const glm::mat4& worldMatrix)
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::vfxruntime::CreateVFXInstanceCommand createCmd;
            createCmd.params.vfxAssetPath = path;
            createCmd.params.worldTransform = worldMatrix;
            createCmd.params.loop = comp.loop;
            createCmd.params.entityId = static_cast<uint32_t>(entity);
            createCmd.params.priority = static_cast<VFXEmitterPriority>(comp.priority);
            createCmd.params.cameraRelative = comp.cameraRelative;
            // This handler destroys it; the Play->Edit purge of play-spawned effects must not.
            createCmd.params.persistent = true;

            const VFXInstanceId instanceId = dispatcher.execute(createCmd);
            if (instanceId == 0)
                return 0;

            if (!comp.overrides.empty())
            {
                events::vfxruntime::ApplyVFXInstanceOverridesCommand overridesCmd;
                overridesCmd.instanceId = instanceId;
                overridesCmd.overrides = toEmitterOverrides(comp.overrides);
                dispatcher.execute(overridesCmd);
            }

            events::vfxruntime::PlayVFXInstanceCommand playCmd;
            playCmd.instanceId = instanceId;
            dispatcher.execute(playCmd);
            return instanceId;
        }
    }

    VFXEditPreviewHandler::~VFXEditPreviewHandler()
    {
        unsubscribeFromEvents();
        unregisterEventHandlers();
    }

    void VFXEditPreviewHandler::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vfxeditpreview::StartVFXEditPreviewCommand>(
            [this](const events::vfxeditpreview::StartVFXEditPreviewCommand& cmd) { return start(cmd.entity); });

        dispatcher.registerCommandHandler<events::vfxeditpreview::StopVFXEditPreviewCommand>(
            [this](const events::vfxeditpreview::StopVFXEditPreviewCommand& cmd) { return stop(cmd.entity); });

        dispatcher.registerQueryHandler<events::vfxeditpreview::GetVFXEditPreviewsQuery>(
            [this](const events::vfxeditpreview::GetVFXEditPreviewsQuery& query) { return list(query.entity); });
    }

    void VFXEditPreviewHandler::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<events::vfxeditpreview::StartVFXEditPreviewCommand>();
        dispatcher.unregisterCommandHandler<events::vfxeditpreview::StopVFXEditPreviewCommand>();
        dispatcher.unregisterQueryHandler<events::vfxeditpreview::GetVFXEditPreviewsQuery>();
    }

    void VFXEditPreviewHandler::subscribeToEvents()
    {
        unsubscribeFromEvents();

        auto& dispatcher = ::events::EventDispatcher::instance();

        // Edit->Play is only dispatched from the MCP drain or the toolbar (render thread idle), so
        // the previews go now: they must never run next to the component's own Play instance.
        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                if (notification.previousMode != EditorMode::Edit || notification.currentMode != EditorMode::Play)
                    return;

                std::lock_guard<std::mutex> lock(previewMutex);
                {
                    std::lock_guard<std::mutex> pendingLock(pendingMutex);
                    pendingStopAll = false;
                    pendingStops.clear();
                }
                if (!previews.empty())
                    vfLogInfo("VFX preview: stopped {} preview(s) on entering Play mode", previews.size());
                destroyAllLocked();
            });

        sceneClearedToken = dispatcher.subscribe<::events::scene::SceneClearedNotification>(
            [this](const ::events::scene::SceneClearedNotification&)
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                pendingStopAll = true;
            });

        // Fires per entity of a deleted subtree, before removal.
        entityDeletedToken = dispatcher.subscribe<::events::scene::EntityDeletedNotification>(
            [this](const ::events::scene::EntityDeletedNotification& notification)
            {
                std::lock_guard<std::mutex> lock(previewMutex);
                if (previews.find(notification.entity) == previews.end())
                    return;
                std::lock_guard<std::mutex> pendingLock(pendingMutex);
                pendingStops.push_back(notification.entity);
            });

        // The renderer drops the saved file's cached config inside its next update (it queues
        // AssetSaved too), so the preview is recreated on the update after that one.
        assetSavedToken = dispatcher.subscribe<::events::resource::AssetSavedNotification>(
            [this](const ::events::resource::AssetSavedNotification& notification)
            {
                std::lock_guard<std::mutex> lock(previewMutex);
                if (previews.empty())
                    return;
                const std::string savedKey = assetKey(notification.filePath);
                for (auto& [entity, preview] : previews)
                {
                    if (assetKey(preview.assetPath) == savedKey)
                        preview.recreateCountdown = 1;
                }
            });
    }

    void VFXEditPreviewHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }

        if (sceneClearedToken.isValid())
        {
            dispatcher.unsubscribe(sceneClearedToken);
            sceneClearedToken = {};
        }

        if (entityDeletedToken.isValid())
        {
            dispatcher.unsubscribe(entityDeletedToken);
            entityDeletedToken = {};
        }

        if (assetSavedToken.isValid())
        {
            dispatcher.unsubscribe(assetSavedToken);
            assetSavedToken = {};
        }
    }

    void VFXEditPreviewHandler::applyPendingStopsLocked()
    {
        bool stopAll = false;
        std::vector<EntityHandle> stops;
        {
            std::lock_guard<std::mutex> pendingLock(pendingMutex);
            stopAll = pendingStopAll;
            pendingStopAll = false;
            stops.swap(pendingStops);
        }

        if (stopAll)
        {
            destroyAllLocked();
            return;
        }

        for (EntityHandle entity : stops)
        {
            auto it = previews.find(entity);
            if (it == previews.end())
                continue;
            destroyPreviewInstance(it->second.instanceId);
            previews.erase(it);
        }
    }

    void VFXEditPreviewHandler::destroyAllLocked()
    {
        for (const auto& [entity, preview] : previews)
            destroyPreviewInstance(preview.instanceId);
        previews.clear();
    }

    VFXEditPreviewStatus VFXEditPreviewHandler::statusOf(EntityHandle entity, const Preview& preview)
    {
        VFXEditPreviewStatus status;
        status.entity = entity;
        status.instanceId = preview.instanceId;
        status.assetPath = preview.assetPath;
        status.loop = preview.loop;
        status.seconds = preview.seconds;
        return status;
    }

    VFXEditPreviewResult VFXEditPreviewHandler::start(EntityHandle entity)
    {
        VFXEditPreviewResult result;
        std::lock_guard<std::mutex> lock(previewMutex);

        // A scene clear or deletion queued before this call must not stop the preview it starts.
        applyPendingStopsLocked();

        if (isPlayMode())
        {
            result.error = "The VFX preview only runs in edit mode; stop Play mode first";
            return result;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            result.error = "The entity does not exist";
            return result;
        }

        const entt::entity enttEntity = internal::fromHandle(entity);
        const auto* comp = registry.try_get<components::VFXComponent>(enttEntity);
        if (!comp)
        {
            result.error = "The entity has no VFX component";
            return result;
        }
        if (!comp->vfxRef.isValid())
        {
            result.error = "The VFX component has no effect assigned";
            return result;
        }
        const auto* world = registry.try_get<components::WorldTransformComponent>(enttEntity);
        if (!world)
        {
            result.error = "The entity has no world transform";
            return result;
        }
        const std::string path = comp->vfxRef.resolve();
        if (path.empty())
        {
            result.error = "The VFX component's effect is not in the asset database";
            return result;
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto it = previews.find(entity);
        if (it != previews.end())
        {
            Preview& preview = it->second;
            const bool recreate = path != preview.assetPath || comp->loop != preview.loop ||
                                  preview.recreateCountdown == 0 || overrideRemoved(preview.applied, comp->overrides);
            if (!recreate)
            {
                // Restart in place, with the component's current overrides and transform.
                if (!vfx::sameOverrides(preview.applied, comp->overrides))
                {
                    events::vfxruntime::ApplyVFXInstanceOverridesCommand overridesCmd;
                    overridesCmd.instanceId = preview.instanceId;
                    overridesCmd.overrides = toEmitterOverrides(comp->overrides);
                    dispatcher.execute(overridesCmd);
                    preview.applied = comp->overrides;
                }
                if (world->worldMatrix != preview.lastWorld)
                {
                    events::vfxruntime::SetVFXInstanceTransformCommand transformCmd;
                    transformCmd.instanceId = preview.instanceId;
                    transformCmd.worldTransform = world->worldMatrix;
                    dispatcher.execute(transformCmd);
                    preview.lastWorld = world->worldMatrix;
                }

                events::vfxruntime::ResetVFXInstanceCommand resetCmd;
                resetCmd.instanceId = preview.instanceId;
                dispatcher.execute(resetCmd);
                preview.seconds = 0.0f;

                result.started = true;
                result.status = statusOf(entity, preview);
                return result;
            }

            destroyPreviewInstance(preview.instanceId);
            previews.erase(it);
        }
        else if (previews.size() >= kMaxPreviews)
        {
            result.error = "At most " + std::to_string(kMaxPreviews) + " VFX previews can run at once; stop one first";
            return result;
        }

        const VFXInstanceId instanceId = createPreviewInstance(enttEntity, *comp, path, world->worldMatrix);
        if (instanceId == 0)
        {
            result.error = "The effect '" + path +
                           "' could not be created: missing file or disabled at this quality tier (see logs_read)";
            return result;
        }

        Preview preview;
        preview.instanceId = instanceId;
        preview.assetPath = path;
        preview.loop = comp->loop;
        preview.applied = comp->overrides;
        preview.lastWorld = world->worldMatrix;

        result.started = true;
        result.status = statusOf(entity, preview);
        previews.insert_or_assign(entity, std::move(preview));
        return result;
    }

    uint32_t VFXEditPreviewHandler::stop(EntityHandle entity)
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        applyPendingStopsLocked();

        if (!entity.isValid())
        {
            const auto stopped = static_cast<uint32_t>(previews.size());
            destroyAllLocked();
            return stopped;
        }

        auto it = previews.find(entity);
        if (it == previews.end())
            return 0;
        destroyPreviewInstance(it->second.instanceId);
        previews.erase(it);
        return 1;
    }

    std::vector<VFXEditPreviewStatus> VFXEditPreviewHandler::list(EntityHandle entity) const
    {
        std::vector<VFXEditPreviewStatus> statuses;
        std::lock_guard<std::mutex> lock(previewMutex);

        // Report what will still be running: queued stops are applied by the next start/stop/update.
        std::vector<EntityHandle> stops;
        {
            std::lock_guard<std::mutex> pendingLock(pendingMutex);
            if (pendingStopAll)
                return statuses;
            stops = pendingStops;
        }
        const auto stopping = [&stops](EntityHandle e)
        {
            return std::find(stops.begin(), stops.end(), e) != stops.end();
        };

        if (entity.isValid())
        {
            auto it = previews.find(entity);
            if (it != previews.end() && !stopping(entity))
                statuses.push_back(statusOf(entity, it->second));
            return statuses;
        }

        statuses.reserve(previews.size());
        for (const auto& [previewEntity, preview] : previews)
        {
            if (!stopping(previewEntity))
                statuses.push_back(statusOf(previewEntity, preview));
        }
        std::sort(statuses.begin(), statuses.end(),
                  [](const VFXEditPreviewStatus& a, const VFXEditPreviewStatus& b) { return a.entity < b.entity; });
        return statuses;
    }

    void VFXEditPreviewHandler::update(float deltaTime)
    {
        std::lock_guard<std::mutex> lock(previewMutex);
        applyPendingStopsLocked();
        if (previews.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();

        for (auto it = previews.begin(); it != previews.end();)
        {
            const EntityHandle entity = it->first;
            Preview& preview = it->second;
            const entt::entity enttEntity = internal::fromHandle(entity);

            // Entity, component, effect or transform gone: the preview ends.
            const auto* comp = registry.valid(enttEntity) ? registry.try_get<components::VFXComponent>(enttEntity)
                                                          : nullptr;
            const auto* world = comp ? registry.try_get<components::WorldTransformComponent>(enttEntity) : nullptr;
            const std::string path = (world && comp->vfxRef.isValid()) ? comp->vfxRef.resolve() : std::string();
            if (path.empty())
            {
                destroyPreviewInstance(preview.instanceId);
                it = previews.erase(it);
                continue;
            }

            const bool recreate = path != preview.assetPath || comp->loop != preview.loop ||
                                  preview.recreateCountdown == 0 || overrideRemoved(preview.applied, comp->overrides);
            if (recreate)
            {
                destroyPreviewInstance(preview.instanceId);
                const VFXInstanceId instanceId = createPreviewInstance(enttEntity, *comp, path, world->worldMatrix);
                if (instanceId == 0)
                {
                    vfLogWarning("VFX preview of entity {} stopped: '{}' could not be recreated", entity.id, path);
                    it = previews.erase(it);
                    continue;
                }

                preview = Preview{};
                preview.instanceId = instanceId;
                preview.assetPath = path;
                preview.loop = comp->loop;
                preview.applied = comp->overrides;
                preview.lastWorld = world->worldMatrix;
                ++it;
                continue;
            }

            // Assignments only, so re-applying the full current set is exact.
            if (!vfx::sameOverrides(preview.applied, comp->overrides))
            {
                events::vfxruntime::ApplyVFXInstanceOverridesCommand overridesCmd;
                overridesCmd.instanceId = preview.instanceId;
                overridesCmd.overrides = toEmitterOverrides(comp->overrides);
                dispatcher.execute(overridesCmd);
                preview.applied = comp->overrides;
            }

            if (world->worldMatrix != preview.lastWorld)
            {
                events::vfxruntime::SetVFXInstanceTransformCommand transformCmd;
                transformCmd.instanceId = preview.instanceId;
                transformCmd.worldTransform = world->worldMatrix;
                dispatcher.execute(transformCmd);
                preview.lastWorld = world->worldMatrix;
            }

            if (preview.recreateCountdown > 0)
                --preview.recreateCountdown;
            preview.seconds += deltaTime;
            ++it;
        }
    }
}
