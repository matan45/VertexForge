#include "ScriptingServiceImpl.hpp"
#include "../events/ScriptingEvents.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/EditorLogger.hpp"
#include <cassert>

namespace services {

    using internal::fromHandle;
    using internal::toHandle;

    ScriptingServiceImpl::ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                                std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : scriptingProvider(scriptingProvider)
        , sceneGraph(sceneGraph) {
        assert(scriptingProvider && "ScriptingProvider must not be null");
        assert(sceneGraph && "SceneGraphSystem must not be null");
    }

    ScriptingServiceImpl::~ScriptingServiceImpl() = default;

    void ScriptingServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===
        dispatcher.registerCommandHandler<events::scripting::AttachScriptCommand>(
            [this](const auto& cmd) {
                return attachScript(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::scripting::DetachScriptCommand>(
            [this](const auto& cmd) {
                detachScript(cmd.entity, cmd.scriptPath);
                return true;
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptEnabledCommand>(
            [this](const auto& cmd) {
                setScriptEnabled(cmd.entity, cmd.scriptPath, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::scripting::TriggerScriptStartCommand>(
            [this](const auto& cmd) {
                triggerStart(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::scripting::TriggerScriptDestroyCommand>(
            [this](const auto& cmd) {
                triggerDestroy(cmd.entity);
            });

        // === Queries ===
        dispatcher.registerQueryHandler<events::scripting::HasScriptQuery>(
            [this](const auto& query) {
                return hasScript(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::IsScriptEnabledQuery>(
            [this](const auto& query) {
                return isScriptEnabled(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPathsQuery>(
            [this](const auto& query) {
                return getScriptPaths(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptDataQuery>(
            [this](const auto& query) -> std::optional<ScriptComponentData> {
                auto enttEntity = internal::fromHandle(query.entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
                    return std::nullopt;
                }
                // Return first script's data, or empty data if no scripts
                const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                ScriptComponentData data;
                if (!scriptComp.scripts.empty()) {
                    data.scriptPath = scriptComp.scripts[0].scriptPath;
                    data.enabled = scriptComp.scripts[0].enabled;
                }
                return data;
            });

        // === Build Commands ===
        dispatcher.registerCommandHandler<events::scripting::BuildScriptsCommand>(
            [this](const auto& cmd) {
                auto result = buildScripts();
                return result.success;
            });

        dispatcher.registerCommandHandler<events::scripting::CleanScriptsCommand>(
            [this](const auto& cmd) {
                cleanScripts();
                return true;
            });

        // === Build Queries ===
        dispatcher.registerQueryHandler<events::scripting::IsScriptsCompiledQuery>(
            [this](const auto& query) {
                return isCompiled();
            });

        // === Mode Change Subscription ===
        dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification) {
                // Stop all scripts when exiting play mode
                if (notification.previousMode == EditorMode::Play &&
                    notification.currentMode == EditorMode::Edit) {
                    stopAllScripts();
                }
            });
    }

    // === Build Methods ===
    ScriptBuildResult ScriptingServiceImpl::buildScripts() {
        vfLogInfo("[Script] Building scripts...");

        auto result = scriptingProvider->buildScripts(DEFAULT_MANIFEST_PATH);

        // Publish notification
        events::scripting::ScriptsBuildCompletedNotification notification;
        notification.success = result.success;
        notification.filesCompiled = result.filesCompiled;
        notification.errors = result.errors;
        ::events::EventDispatcher::instance().publish(notification);

        // Load the compiled scripts if build was successful
        if (result.success) {
            scriptingProvider->loadCompiledScripts(DEFAULT_MANIFEST_PATH);
        }

        return result;
    }

    void ScriptingServiceImpl::cleanScripts() {
        vfLogInfo("[Script] Cleaning scripts...");
        scriptingProvider->cleanScripts(DEFAULT_MANIFEST_PATH);
    }

    bool ScriptingServiceImpl::isCompiled() const {
        return scriptingProvider->isCompiled();
    }

    bool ScriptingServiceImpl::attachScript(EntityHandle entity, const ScriptData& data) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        // Add ScriptComponent if not present
        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            registry.emplace<components::ScriptComponent>(enttEntity);
        }

        // If no script path provided, just ensure component exists (for "Add Component" UI flow)
        if (data.scriptPath.empty()) {
            vfLogInfo("[Script] Added empty ScriptComponent to entity");
            return true;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);

        // Check if script already attached
        if (scriptComp.hasScript(data.scriptPath)) {
            vfLogWarning("[Script] Script '{}' already attached to entity", data.scriptPath);
            return false;
        }

        // Just store the script path - actual loading happens when Play is pressed
        components::ScriptEntry entry;
        entry.scriptPath = data.scriptPath;
        entry.enabled = data.enabled;

        scriptComp.scripts.push_back(entry);

        vfLogInfo("[Script] Attached script '{}' to entity (total scripts: {})",
                  data.scriptPath, scriptComp.scripts.size());
        return true;
    }

    void ScriptingServiceImpl::detachScript(EntityHandle entity, const std::string& scriptPath) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);

        if (!entry) {
            return;
        }

        // Call onDestroy if script was started
        if (entry->started && entry->hasOnDestroy) {
            scriptingProvider->callOnDestroy(entry->instanceId);
        }

        // Unload from provider
        scriptingProvider->unloadScript(entry->instanceId);

        // Remove from component
        scriptComp.removeByPath(scriptPath);

        // Remove component entirely if no scripts left
        if (scriptComp.scripts.empty()) {
            registry.remove<components::ScriptComponent>(enttEntity);
        }

        // Publish notification
        events::scripting::ScriptDetachedNotification detachedNotification;
        detachedNotification.entity = entity;
        detachedNotification.scriptPath = scriptPath;
        ::events::EventDispatcher::instance().publish(detachedNotification);

        vfLogInfo("[Script] Detached script '{}' from entity", scriptPath);
    }

    void ScriptingServiceImpl::detachAllScripts(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);

        // Detach each script
        for (auto& entry : scriptComp.scripts) {
            if (entry.started && entry.hasOnDestroy) {
                scriptingProvider->callOnDestroy(entry.instanceId);
            }
            scriptingProvider->unloadScript(entry.instanceId);

            events::scripting::ScriptDetachedNotification detachedNotification;
            detachedNotification.entity = entity;
            detachedNotification.scriptPath = entry.scriptPath;
            ::events::EventDispatcher::instance().publish(detachedNotification);
        }

        // Remove component
        registry.remove<components::ScriptComponent>(enttEntity);

        vfLogInfo("[Script] Detached all scripts from entity");
    }

    bool ScriptingServiceImpl::hasScripts(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        return !registry.get<components::ScriptComponent>(enttEntity).scripts.empty();
    }

    bool ScriptingServiceImpl::hasScript(EntityHandle entity, const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        return registry.get<components::ScriptComponent>(enttEntity).hasScript(scriptPath);
    }

    std::vector<std::string> ScriptingServiceImpl::getScriptPaths(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<std::string> paths;

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return paths;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (const auto& entry : scriptComp.scripts) {
            paths.push_back(entry.scriptPath);
        }
        return paths;
    }

    void ScriptingServiceImpl::setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);
        if (entry) {
            entry->enabled = enabled;
        }
    }

    bool ScriptingServiceImpl::isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return false;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        return entry ? entry->enabled : false;
    }

    void ScriptingServiceImpl::updateScripts(float deltaTime) {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate all entities with ScriptComponent
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view) {
            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entity)) {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) {
                    continue;
                }
            }

            auto& scriptComp = view.get<components::ScriptComponent>(entity);

            // Update each script on this entity
            for (auto& entry : scriptComp.scripts) {
                if (!entry.enabled) {
                    continue;
                }

                // If instanceId is 0, script needs to be loaded (e.g., after scene restore)
                if (entry.instanceId == 0 && !entry.scriptPath.empty()) {
                    auto info = scriptingProvider->loadScript(entry.scriptPath, toHandle(entity));
                    if (info.has_value()) {
                        entry.instanceId = info->instanceId;
                        entry.hasOnStart = info->hasOnStart;
                        entry.hasOnUpdate = info->hasOnUpdate;
                        entry.hasOnDestroy = info->hasOnDestroy;
                    } else {
                        continue;
                    }
                }

                // Call onStart if not started yet
                if (!entry.started && entry.hasOnStart) {
                    vfLogInfo("[Script] Calling onStart for script '{}' (instance {})",
                              entry.scriptPath, entry.instanceId);
                    scriptingProvider->callOnStart(entry.instanceId);
                    entry.started = true;

                    // Publish notification
                    events::scripting::ScriptStartedNotification startedNotification;
                    startedNotification.entity = toHandle(entity);
                    startedNotification.scriptPath = entry.scriptPath;
                    ::events::EventDispatcher::instance().publish(startedNotification);
                }

                // Call onUpdate
                if (entry.hasOnUpdate) {
                    scriptingProvider->callOnUpdate(entry.instanceId, deltaTime);
                }
            }
        }
    }

    void ScriptingServiceImpl::triggerStart(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (auto& entry : scriptComp.scripts) {
            if (!entry.started && entry.hasOnStart) {
                scriptingProvider->callOnStart(entry.instanceId);
                entry.started = true;
            }
        }
    }

    void ScriptingServiceImpl::triggerDestroy(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (auto& entry : scriptComp.scripts) {
            if (entry.hasOnDestroy) {
                scriptingProvider->callOnDestroy(entry.instanceId);
            }
        }
    }

    void ScriptingServiceImpl::stopAllScripts() {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view) {
            auto& scriptComp = view.get<components::ScriptComponent>(entity);
            for (auto& entry : scriptComp.scripts) {
                // Call onDestroy for started scripts
                if (entry.started && entry.hasOnDestroy) {
                    scriptingProvider->callOnDestroy(entry.instanceId);
                }
                // Reset script entry state
                entry.instanceId = 0;
                entry.started = false;
            }
        }

        // Unload all scripts from provider and reset instance counter
        scriptingProvider->unloadAllScripts();
        vfLogInfo("[Script] All scripts stopped");
    }

}
