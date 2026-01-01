#include "ScriptingServiceImpl.hpp"
#include "../events/ScriptingEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include "../data/EntityConversion.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include <cassert>
#include <spdlog/spdlog.h>

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
                detachScript(cmd.entity);
                return true;
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptEnabledCommand>(
            [this](const auto& cmd) {
                setScriptEnabled(cmd.entity, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptPropertyCommand>(
            [this](const auto& cmd) {
                return setProperty(cmd.entity, cmd.propertyName, cmd.value);
            });

        dispatcher.registerCommandHandler<events::scripting::CallScriptMethodCommand>(
            [this](const auto& cmd) {
                return callMethod(cmd.entity, cmd.methodName, cmd.args);
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
                return hasScript(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::IsScriptEnabledQuery>(
            [this](const auto& query) {
                return isScriptEnabled(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptDataQuery>(
            [this](const auto& query) -> std::optional<ScriptComponentData> {
                if (!hasScript(query.entity)) {
                    return std::nullopt;
                }
                auto entity = fromHandle(query.entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (registry.all_of<components::ScriptComponent>(entity)) {
                    auto& script = registry.get<components::ScriptComponent>(entity);
                    return ScriptComponentData{script.scriptPath, script.enabled};
                }
                return std::nullopt;
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPropertiesQuery>(
            [this](const auto& query) {
                return getScriptProperties(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPropertyQuery>(
            [this](const auto& query) {
                return getProperty(query.entity, query.propertyName);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptMethodsQuery>(
            [this](const auto& query) {
                return getScriptMethods(query.entity);
            });
    }

    bool ScriptingServiceImpl::attachScript(EntityHandle entity, const ScriptData& data) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        // Add ScriptComponent if not present
        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            registry.emplace<components::ScriptComponent>(enttEntity);
        }

        auto& script = registry.get<components::ScriptComponent>(enttEntity);
        script.scriptPath = data.scriptPath;
        script.enabled = data.enabled;
        script.started = false;

        // Load the script via provider
        auto info = scriptingProvider->loadScript(data.scriptPath, entity);
        if (!info.has_value()) {
            // Script failed to load - publish error notification
            auto error = scriptingProvider->getLastError();
            if (error.has_value()) {
                events::scripting::ScriptErrorNotification errorNotification;
                errorNotification.entity = entity;
                errorNotification.error = error.value();
                ::events::EventDispatcher::instance().publish(errorNotification);
            }
            return false;
        }

        // Store instance info in component
        script.instanceId = info->instanceId;
        script.hasOnStart = info->hasOnStart;
        script.hasOnUpdate = info->hasOnUpdate;
        script.hasOnDestroy = info->hasOnDestroy;

        // Publish success notification
        events::scripting::ScriptAttachedNotification attachedNotification;
        attachedNotification.entity = entity;
        attachedNotification.scriptPath = data.scriptPath;
        attachedNotification.className = info->className;
        ::events::EventDispatcher::instance().publish(attachedNotification);

        spdlog::info("[ScriptingService] Attached script '{}' to entity", data.scriptPath);
        return true;
    }

    void ScriptingServiceImpl::detachScript(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity)) {
            return;
        }

        auto& script = registry.get<components::ScriptComponent>(enttEntity);

        // Call onDestroy if script was started
        if (script.started && script.hasOnDestroy) {
            scriptingProvider->callOnDestroy(script.instanceId);
        }

        // Unload from provider
        scriptingProvider->unloadScript(script.instanceId);

        // Remove component
        registry.remove<components::ScriptComponent>(enttEntity);

        // Publish notification
        events::scripting::ScriptDetachedNotification detachedNotification;
        detachedNotification.entity = entity;
        ::events::EventDispatcher::instance().publish(detachedNotification);
    }

    bool ScriptingServiceImpl::hasScript(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        return registry.all_of<components::ScriptComponent>(enttEntity);
    }

    void ScriptingServiceImpl::setScriptEnabled(EntityHandle entity, bool enabled) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            registry.get<components::ScriptComponent>(enttEntity).enabled = enabled;
        }
    }

    bool ScriptingServiceImpl::isScriptEnabled(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            return registry.get<components::ScriptComponent>(enttEntity).enabled;
        }
        return false;
    }

    std::vector<ScriptPropertyInfo> ScriptingServiceImpl::getScriptProperties(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            return scriptingProvider->getProperties(script.instanceId);
        }
        return {};
    }

    bool ScriptingServiceImpl::setProperty(EntityHandle entity, const std::string& propertyName,
                                            const std::any& value) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            return scriptingProvider->setProperty(script.instanceId, propertyName, value);
        }
        return false;
    }

    std::optional<std::any> ScriptingServiceImpl::getProperty(EntityHandle entity,
                                                               const std::string& propertyName) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            return scriptingProvider->getProperty(script.instanceId, propertyName);
        }
        return std::nullopt;
    }

    std::vector<ScriptMethodInfo> ScriptingServiceImpl::getScriptMethods(EntityHandle entity) const {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            return scriptingProvider->getMethods(script.instanceId);
        }
        return {};
    }

    std::optional<std::any> ScriptingServiceImpl::callMethod(EntityHandle entity,
                                                              const std::string& methodName,
                                                              const std::vector<std::any>& args) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            return scriptingProvider->callMethod(script.instanceId, methodName, args);
        }
        return std::nullopt;
    }

    void ScriptingServiceImpl::sendMessage(EntityHandle entity, const std::string& messageName,
                                            const std::any& data) {
        // TODO: Implement message sending to script
    }

    void ScriptingServiceImpl::broadcastMessage(const std::string& messageName,
                                                 const std::any& data) {
        // TODO: Implement broadcast message to all scripts
    }

    void ScriptingServiceImpl::updateScripts(float deltaTime) {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Iterate all entities with ScriptComponent
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view) {
            auto& script = view.get<components::ScriptComponent>(entity);

            if (!script.enabled) {
                continue;
            }

            // Call onStart if not started yet
            if (!script.started && script.hasOnStart) {
                scriptingProvider->callOnStart(script.instanceId);
                script.started = true;

                // Publish notification
                events::scripting::ScriptStartedNotification startedNotification;
                startedNotification.entity = toHandle(entity);
                ::events::EventDispatcher::instance().publish(startedNotification);
            }

            // Call onUpdate
            if (script.hasOnUpdate) {
                scriptingProvider->callOnUpdate(script.instanceId, deltaTime);
            }
        }
    }

    void ScriptingServiceImpl::fixedUpdate(float fixedDeltaTime) {
        // TODO: Implement fixedUpdate when needed
    }

    void ScriptingServiceImpl::lateUpdate(float deltaTime) {
        // TODO: Implement lateUpdate when needed
    }

    void ScriptingServiceImpl::triggerStart(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            if (!script.started && script.hasOnStart) {
                scriptingProvider->callOnStart(script.instanceId);
                script.started = true;
            }
        }
    }

    void ScriptingServiceImpl::triggerDestroy(EntityHandle entity) {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (registry.all_of<components::ScriptComponent>(enttEntity)) {
            auto& script = registry.get<components::ScriptComponent>(enttEntity);
            if (script.hasOnDestroy) {
                scriptingProvider->callOnDestroy(script.instanceId);
            }
        }
    }

    bool ScriptingServiceImpl::reloadScript(const std::string& scriptPath) {
        // TODO: Implement hot-reload
        return false;
    }

    void ScriptingServiceImpl::reloadAllScripts() {
        // TODO: Implement hot-reload all
    }

}
