// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <project/ProjectBuilder.hpp>
#include <project/ProjectConfigParser.hpp>

#include "ScriptingAdapter.hpp"
#include "ScriptUIEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <array>

#include "print/EditorLogger.hpp"
#include "events/PhysicsEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core
{
    ScriptingAdapter::ScriptingAdapter() = default;

    ScriptingAdapter::~ScriptingAdapter()
    {
        cleanUp();
    }

    bool ScriptingAdapter::init()
    {
        if (initialized)
        {
            return true;
        }

        try
        {
            interpreter = std::make_unique<::services::ScriptInterpreter>();

            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
            apiRegistry->registerEngineAPIs();

            uiEventBridge = std::make_unique<ScriptUIEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            subscribeToPhysicsEvents();
            uiEventBridge->subscribeAll();

            initialized = true;
            vfLogInfo("[ScriptingAdapter] Initialized mType scripting system");
            return true;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to initialize scripting system: ") + e.what());
            vfLogError("[Script] Init failed: {}", e.what());
            return false;
        }
    }

    void ScriptingAdapter::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        unsubscribeFromPhysicsEvents();
        if (uiEventBridge)
        {
            uiEventBridge->unsubscribeAll();
        }

        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        pathToClassName.clear();

        apiRegistry.reset();
        interpreter.reset();
        initialized = false;

        vfLogInfo("[ScriptingAdapter] Cleaned up scripting system");
    }

    bool ScriptingAdapter::isInitialized() const
    {
        return initialized;
    }

    services::ScriptBuildResult ScriptingAdapter::buildScripts(const std::string& manifestPath)
    {
        services::ScriptBuildResult result;

        if (!initialized)
        {
            result.success = false;
            result.errors.push_back("Scripting system not initialized");
            return result;
        }

        try
        {
            vfLogInfo("[ScriptingAdapter] Building scripts from manifest: {}", manifestPath);

            cleanScripts(manifestPath);

            project::ProjectConfigParser parser;
            auto config = parser.parse(manifestPath);

            if (!config)
            {
                result.success = false;
                result.errors.push_back("Failed to parse manifest: " + manifestPath);
                return result;
            }

            project::ProjectBuilder builder;
            std::string libraryPath = getLibraryPath(manifestPath);
            auto buildResult = builder.buildLibrary(*config, libraryPath, interpreter->getEnvironment());

            result.success = buildResult.success;
            result.filesCompiled = buildResult.filesCompiled;
            result.filesFailed = buildResult.filesFailed;
            result.errors = buildResult.errors;

            if (result.success)
            {
                compiled = true;
                vfLogInfo("[ScriptingAdapter] Build successful: {} files compiled", result.filesCompiled);
                vfLogInfo("[Script] Build successful: {} files compiled", result.filesCompiled);
            }
            else
            {
                compiled = false;
                vfLogError("[ScriptingAdapter] Build failed with {} errors", result.errors.size());
                for (const auto& error : result.errors)
                {
                    vfLogError("[Script] {}", error);
                }
            }

            return result;
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.errors.push_back(e.what());
            setError(services::ScriptError::Type::Compile, e.what());
            vfLogError("[Script] Build failed: {}", e.what());
            return result;
        }
    }

    void ScriptingAdapter::cleanScripts(const std::string& manifestPath)
    {
        try
        {
            vfLogInfo("[Script] Cleaning scripts...");

            project::ProjectConfigParser parser;
            auto config = parser.parse(manifestPath);

            if (config)
            {
                project::ProjectBuilder builder;
                builder.clean(*config);
            }

            pathToClassName.clear();
            compiled = false;

            vfLogInfo("[ScriptingAdapter] Clean completed");
            vfLogInfo("[Script] Clean completed");
        }
        catch (const std::exception& e)
        {
            vfLogError("[ScriptingAdapter] Clean failed: {}", e.what());
            vfLogError("[Script] Clean failed: {}", e.what());
        }
    }

    bool ScriptingAdapter::isCompiled() const
    {
        return compiled;
    }

    bool ScriptingAdapter::loadCompiledScripts(const std::string& manifestPath)
    {
        if (!initialized)
        {
            vfLogError("[ScriptingAdapter] Cannot load scripts: not initialized");
            return false;
        }

        if (!compiled)
        {
            vfLogWarning("[ScriptingAdapter] Scripts not compiled. Call buildScripts first.");
            return false;
        }

        try
        {
            std::string libraryPath = getLibraryPath(manifestPath);

            if (!std::filesystem::exists(libraryPath))
            {
                vfLogError("[ScriptingAdapter] Compiled library not found: {}", libraryPath);
                return false;
            }

            vfLogInfo("[ScriptingAdapter] Loading compiled scripts from: {}", libraryPath);
            interpreter->loadCompiledBytecode(libraryPath);

            vfLogInfo("[ScriptingAdapter] Compiled scripts loaded successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to load compiled scripts: ") + e.what());
            vfLogError("[Script] Failed to load compiled scripts: {}", e.what());
            return false;
        }
    }

    std::string ScriptingAdapter::getLibraryPath(const std::string& manifestPath) const
    {
        std::filesystem::path manifestDir = std::filesystem::path(manifestPath).parent_path();
        return (manifestDir / "compiled" / "scripts.mtcLib").string();
    }

    std::optional<services::ScriptInstanceInfo> ScriptingAdapter::loadScript(
        const std::string& scriptPath,
        services::EntityHandle entity)
    {
        if (!initialized)
        {
            setError(services::ScriptError::Type::Runtime, "Scripting system not initialized");
            return std::nullopt;
        }

        if (!compiled)
        {
            setError(services::ScriptError::Type::Runtime,
                     "Scripts not compiled. Click 'Build Scripts' first.");
            vfLogError("[Script] Scripts not compiled. Click 'Build Scripts' first.");
            return std::nullopt;
        }

        try
        {
            std::string fullPath = scriptLibraryPath.empty() ? scriptPath : scriptLibraryPath + "/" + scriptPath;

            std::string className;
            auto pathIt = pathToClassName.find(scriptPath);
            if (pathIt != pathToClassName.end())
            {
                className = pathIt->second;
            }
            else
            {
                className = extractClassName(fullPath);
                if (className.empty())
                {
                    setError(services::ScriptError::Type::Compile,
                             "Could not find class definition in script", scriptPath);
                    return std::nullopt;
                }
                pathToClassName[scriptPath] = className;
            }

            auto instance = interpreter->createObject(className);
            uint64_t instanceId = nextInstanceId++;

            instanceToClassName[instanceId] = className;
            instanceToEntity[instanceId] = entity;
            instanceToObject[instanceId] = std::any(instance);

            // Cache implemented interfaces for collision/trigger/UI callbacks
            static constexpr std::array<const char*, 9> kCheckedInterfaces = {
                "ICollisionListener", "ITriggerListener",
                "IUIButtonListener", "IUITextInputListener", "IUICheckboxListener",
                "IUIDropdownListener", "IUITabsListener", "IUISliderListener",
                "IUIProgressBarListener"
            };

            std::unordered_set<std::string> interfaces;
            for (const auto* iface : kCheckedInterfaces)
            {
                if (interpreter->classImplementsInterface(className, iface))
                {
                    interfaces.insert(iface);
                }
            }
            instanceToInterfaces[instanceId] = std::move(interfaces);

            instanceToPlaybackState[instanceId] = services::ScriptPlaybackState::Stopped;

            services::ScriptInstanceInfo info;
            info.instanceId = instanceId;
            info.className = className;
            info.scriptPath = scriptPath;

            vfLogInfo("[ScriptingAdapter] Loaded script '{}' as class '{}' (instanceId={})",
                      scriptPath, className, instanceId);

            return info;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime, e.what(), scriptPath);
            vfLogError("[Script] Failed to create script instance '{}': {}", scriptPath, e.what());
            return std::nullopt;
        }
    }

    void ScriptingAdapter::unloadScript(uint64_t instanceId)
    {
        auto it = instanceToClassName.find(instanceId);
        if (it != instanceToClassName.end())
        {
            instanceToClassName.erase(it);
            instanceToEntity.erase(instanceId);
            instanceToObject.erase(instanceId);
            instanceToInterfaces.erase(instanceId);
            instanceToPlaybackState.erase(instanceId);
        }
    }

    void ScriptingAdapter::unloadAllScripts()
    {
        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        instanceToInterfaces.clear();
        instanceToPlaybackState.clear();
        nextInstanceId = 1;
        vfLogInfo("[ScriptingAdapter] All scripts unloaded, instance counter reset");
    }

    bool ScriptingAdapter::isScriptLoaded(uint64_t instanceId) const
    {
        return instanceToClassName.find(instanceId) != instanceToClassName.end();
    }

    void ScriptingAdapter::callOnStart(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] callOnStart: script {} not loaded", instanceId);
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onStart", {});
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onStart failed: ") + e.what());
            vfLogError("[Script] onStart failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnUpdate(uint64_t instanceId, float deltaTime)
    {
        if (!isScriptLoaded(instanceId))
        {
            return;
        }

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
        {
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onUpdate", {value::Value(deltaTime)});
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            vfLogError("[Script] onUpdate failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onDestroy", {});
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            vfLogError("[Script] onDestroy failed: {}", e.what());
        }
    }

    void ScriptingAdapter::playVFX(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] playScript: script {} not loaded", instanceId);
            return;
        }

        auto& state = instanceToPlaybackState[instanceId];
        if (state == services::ScriptPlaybackState::Stopped)
        {
            callOnStart(instanceId);
        }
        state = services::ScriptPlaybackState::Playing;
        vfLogInfo("[ScriptingAdapter] Script {} now playing", instanceId);
    }

    std::optional<services::ScriptError> ScriptingAdapter::getLastError() const
    {
        return lastError;
    }

    void ScriptingAdapter::clearError()
    {
        lastError = std::nullopt;
    }

    void ScriptingAdapter::setScriptLibraryPath(const std::string& path)
    {
        scriptLibraryPath = path;
        vfLogInfo("[ScriptingAdapter] Script library path set to: {}", path);
    }

    void ScriptingAdapter::setError(services::ScriptError::Type type, const std::string& message,
                                    const std::string& file, int line)
    {
        lastError = services::ScriptError{type, message, file, line, 0};
    }

    std::string ScriptingAdapter::extractClassName(const std::string& scriptPath)
    {
        std::ifstream file(scriptPath);
        if (!file.is_open())
        {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Pattern: @Script followed by optional whitespace/newlines, then class ClassName
        std::regex scriptAnnotationPattern(R"(@Script\s+(?:public\s+)?class\s+(\w+)\b)");
        std::smatch match;

        if (std::regex_search(content, match, scriptAnnotationPattern))
        {
            return match[1].str();
        }

        // Fallback for backwards compatibility
        std::regex anyClassPattern(R"(\bclass\s+(\w+)\b)");
        if (std::regex_search(content, match, anyClassPattern))
        {
            return match[1].str();
        }

        return "";
    }

    void ScriptingAdapter::subscribeToPhysicsEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        collisionStartToken = dispatcher.subscribe<::events::physics::CollisionStartNotification>(
            [this](const ::events::physics::CollisionStartNotification& notif)
            {
                dispatchCollisionCallback("onCollisionEnter", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionEnter", notif.entityB, notif.entityA);
            });

        collisionEndToken = dispatcher.subscribe<::events::physics::CollisionEndNotification>(
            [this](const ::events::physics::CollisionEndNotification& notif)
            {
                dispatchCollisionCallback("onCollisionExit", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionExit", notif.entityB, notif.entityA);
            });

        triggerEnterToken = dispatcher.subscribe<::events::physics::TriggerEnterNotification>(
            [this](const ::events::physics::TriggerEnterNotification& notif)
            {
                dispatchCollisionCallback("onTriggerEnter", notif.triggerEntity, notif.otherEntity);
            });

        triggerExitToken = dispatcher.subscribe<::events::physics::TriggerExitNotification>(
            [this](const ::events::physics::TriggerExitNotification& notif)
            {
                dispatchCollisionCallback("onTriggerExit", notif.triggerEntity, notif.otherEntity);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to physics collision events");
    }

    void ScriptingAdapter::unsubscribeFromPhysicsEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (collisionStartToken.isValid())
        {
            dispatcher.unsubscribe(collisionStartToken);
        }
        if (collisionEndToken.isValid())
        {
            dispatcher.unsubscribe(collisionEndToken);
        }
        if (triggerEnterToken.isValid())
        {
            dispatcher.unsubscribe(triggerEnterToken);
        }
        if (triggerExitToken.isValid())
        {
            dispatcher.unsubscribe(triggerExitToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from physics collision events");
    }

    void ScriptingAdapter::dispatchCollisionCallback(const char* methodName,
                                                     ::services::EntityHandle self, ::services::EntityHandle other)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.valid(static_cast<entt::entity>(self.id)))
        {
            return;
        }

        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(self.id));

        if (!scriptComp)
        {
            return;
        }

        std::string requiredInterface;
        std::string methodStr(methodName);
        if (methodStr == "onCollisionEnter" || methodStr == "onCollisionExit")
        {
            requiredInterface = "ICollisionListener";
        }
        else if (methodStr == "onTriggerEnter" || methodStr == "onTriggerExit")
        {
            requiredInterface = "ITriggerListener";
        }
        else
        {
            return;
        }

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id == self.id)
            {
                auto interfaceIt = instanceToInterfaces.find(instanceId);
                if (interfaceIt == instanceToInterfaces.end() ||
                    interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                {
                    continue;
                }

                auto objIt = instanceToObject.find(instanceId);
                if (objIt != instanceToObject.end())
                {
                    try
                    {
                        NativeAPIRegistry::setCurrentEntity(self);
                        auto& instance = std::any_cast<value::Value&>(objIt->second);
                        interpreter->callMethod(instance, methodName,
                                                {value::Value(static_cast<int>(other.id))});
                    }
                    catch (const std::exception& e)
                    {
                        vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
                    }
                }
            }
        }
    }
}
