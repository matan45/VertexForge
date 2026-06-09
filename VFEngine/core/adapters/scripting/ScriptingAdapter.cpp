// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <value/ValueShim.hpp>
#include <project/ProjectBuilder.hpp>
#include <project/ProjectConfigParser.hpp>
#include <project/mtclib/MtcLibSerializer.hpp>

#include "ScriptingAdapter.hpp"
#include "ScriptUIEventBridge.hpp"
#include "ScriptPhysicsEventBridge.hpp"
#include "ScriptAnimationEventBridge.hpp"
#include "ScriptSocketEventBridge.hpp"
#include "ScriptVFXEventBridge.hpp"
#include "ScriptNavigationEventBridge.hpp"
#include "ScriptInputActionEventBridge.hpp"
#include "ScriptSceneEventBridge.hpp"
#include "ScriptWeatherEventBridge.hpp"
#include "ScriptDestructionEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "CoroutineManager.hpp"
#include "ScriptCommunicationManager.hpp"
#include "ScriptDebugServer.hpp"
#include "../api/CoroutineAPI.hpp"
#include "../api/ScriptCommunicationAPI.hpp"
#include "../api/PluginComponentAPI.hpp"
#include "../../../plugin/core/PluginContextImpl.hpp"
#include <runtime/EventLoop.hpp>
#include <vm/runtime/VirtualMachine.hpp>
#include <environment/Environment.hpp>
#include <environment/registry/NativeRegistry.hpp>
#include <plugin/PluginHost.hpp>
#include <json/JsonSerializer.hpp>
#include <json/JsonDeserializer.hpp>
#include <value/ObjectInstance.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <array>


#include "print/Log.hpp"
namespace core
{
    ScriptingAdapter::ScriptingAdapter() = default;

    ScriptingAdapter::~ScriptingAdapter()
    {
        cleanUp();
    }

    bool ScriptingAdapter::init()
    {
        if (initialized) return true;

        try
        {
            interpreter = std::make_unique<::services::ScriptInterpreter>();

            coroutineManager = std::make_unique<CoroutineManager>();
            debugServer = std::make_unique<ScriptDebugServer>();

            communicationManager = std::make_unique<ScriptCommunicationManager>(
                interpreter.get(), instanceToClassName, instanceToEntity, instanceToObject);

            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
            api::CoroutineAPI::setCoroutineManager(coroutineManager.get());
            api::ScriptCommunicationAPI::setManager(communicationManager.get());
            apiRegistry->registerEngineAPIs();

            // VK-1290: Set callback so Plugin module can trigger script binding registration
            auto* interp = interpreter.get();
            plugin::PluginContextImpl::setScriptBindingRegistrar(
                [interp](const std::vector<plugin::MetaComponentBridge>& bridges) {
                    api::PluginComponentAPI::registerAPI(interp, bridges);
                });

            // API v10: hand engine plugins mType's plugin host vtable so their
            // registered natives can build/inspect script values through the
            // standard C ABI (all execution stays engine-side in mType.lib).
            plugin::PluginContextImpl::setScriptHostVTable(::plugin::getHostVTable());

            uiEventBridge = std::make_unique<ScriptUIEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            physicsEventBridge = std::make_unique<ScriptPhysicsEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            animationEventBridge = std::make_unique<ScriptAnimationEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            socketEventBridge = std::make_unique<ScriptSocketEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            vfxEventBridge = std::make_unique<ScriptVFXEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            navigationEventBridge = std::make_unique<ScriptNavigationEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            inputActionEventBridge = std::make_unique<ScriptInputActionEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity, instanceToPriority);

            sceneEventBridge = std::make_unique<ScriptSceneEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            weatherEventBridge = std::make_unique<ScriptWeatherEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);
            destructionEventBridge = std::make_unique<ScriptDestructionEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            physicsEventBridge->subscribeAll();
            uiEventBridge->subscribeAll();
            animationEventBridge->subscribeAll();
            socketEventBridge->subscribeAll();
            vfxEventBridge->subscribeAll();
            navigationEventBridge->subscribeAll();
            inputActionEventBridge->subscribeAll();
            sceneEventBridge->subscribeAll();
            weatherEventBridge->subscribeAll();
            destructionEventBridge->subscribeAll();

            initialized = true;
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
        if (!initialized) return;

        // Stop the debug server first so no script thread is parked at a breakpoint
        // while we tear the interpreter down.
        if (debugServer) debugServer->stop();
        debugServer.reset();

        if (weatherEventBridge) weatherEventBridge->unsubscribeAll();
        if (destructionEventBridge) destructionEventBridge->unsubscribeAll();
        if (sceneEventBridge) sceneEventBridge->unsubscribeAll();
        if (inputActionEventBridge) inputActionEventBridge->unsubscribeAll();
        if (navigationEventBridge) navigationEventBridge->unsubscribeAll();
        if (vfxEventBridge) vfxEventBridge->unsubscribeAll();
        if (socketEventBridge) socketEventBridge->unsubscribeAll();
        if (animationEventBridge) animationEventBridge->unsubscribeAll();
        if (physicsEventBridge) physicsEventBridge->unsubscribeAll();
        if (uiEventBridge) uiEventBridge->unsubscribeAll();

        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        pathToClassName.clear();

        api::PluginComponentAPI::cleanup();
        apiRegistry.reset();
        interpreter.reset();
        pluginNativeBindings.clear();  // after the interpreter (and its registry entries) is gone
        initialized = false;

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
                // Register mType classes for plugin struct types (requires stdlib loaded)
                api::PluginComponentAPI::registerStructClasses(interpreter.get());
                vfLogInfo("[ScriptingAdapter] Build successful: {} files compiled", result.filesCompiled);
            }
            else
            {
                compiled = false;
                vfLogError("[ScriptingAdapter] Build failed with {} errors", result.errors.size());
                for (const auto& error : result.errors)
                    vfLogError("[Script] {}", error);
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

            if (interpreter)
            {
                interpreter->resetForRebuild();
            }

            pathToClassName.clear();
            compiled = false;

            vfLogInfo("[ScriptingAdapter] Clean completed");
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

            // Unwrap the .mtcLib container and load its embedded BytecodeProgram
            // as the main program so createObject() can find script classes.
            // (loadLibrary registers classes for cross-library imports only —
            // it does not set the cached program needed by createObject.)
            std::ifstream libFile(libraryPath, std::ios::binary);
            if (!libFile)
            {
                vfLogError("[ScriptingAdapter] Could not open compiled library: {}", libraryPath);
                return false;
            }
            auto libProgram = project::mtclib::MtcLibSerializer::deserialize(libFile);
            if (libProgram.bytecodeProgram.getInstructions().empty() &&
                libProgram.bytecodeProgram.getClasses().empty())
            {
                vfLogError("[ScriptingAdapter] Compiled library is empty: {}", libraryPath);
                return false;
            }
            interpreter->loadFromProgram(std::move(libProgram.bytecodeProgram));

            // Register mType classes for plugin struct types (requires stdlib loaded)
            api::PluginComponentAPI::registerStructClasses(interpreter.get());

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

            // If path doesn't exist (e.g. relative path from scene), try absolute resolve
            if (!std::filesystem::exists(fullPath))
            {
                // Try the original scriptPath directly (may be absolute from assetdb)
                if (std::filesystem::exists(scriptPath))
                {
                    fullPath = scriptPath;
                }
            }

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
            static constexpr std::array<const char*, 16> kCheckedInterfaces = {
                "ICollisionListener", "ITriggerListener",
                "IUIButtonListener", "IUITextInputListener", "IUICheckboxListener",
                "IUIDropdownListener", "IUITabsListener", "IUISliderListener",
                "IUIProgressBarListener", "IUIDragDropListener",
                "IAnimationEventListener",
                "ISocketAttachmentListener",
                "IVFXEventListener",
                "INavigationEventListener",
                "IInputActionListener",
                "IWeatherEventListener"
            };

            std::unordered_set<std::string> interfaces;
            for (const auto* iface : kCheckedInterfaces)
            {
                if (interpreter->classImplementsInterface(className, iface))
                    interfaces.insert(iface);
            }
            instanceToInterfaces[instanceId] = std::move(interfaces);
            instanceToPlaybackState[instanceId] = services::ScriptPlaybackState::Stopped;
            instanceToPriority[instanceId] = 0;

            services::ScriptInstanceInfo info;
            info.instanceId = instanceId;
            info.className = className;
            info.scriptPath = scriptPath;

            vfLogDebug("[ScriptingAdapter] Loaded script '{}' as class '{}' (instanceId={})",
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
        if (coroutineManager)
        {
            coroutineManager->removeAllForInstance(instanceId);
        }

        if (communicationManager)
        {
            communicationManager->removeListenersForInstance(instanceId);
        }

        auto it = instanceToClassName.find(instanceId);
        if (it != instanceToClassName.end())
        {
            instanceToClassName.erase(it);
            instanceToEntity.erase(instanceId);
            instanceToObject.erase(instanceId);
            instanceToInterfaces.erase(instanceId);
            instanceToPlaybackState.erase(instanceId);
            instanceToPriority.erase(instanceId);
        }
    }

    void ScriptingAdapter::unloadAllScripts()
    {
        if (coroutineManager)
        {
            coroutineManager->clear();
        }

        if (communicationManager)
        {
            communicationManager->clearAll();
        }

        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        instanceToInterfaces.clear();
        instanceToPlaybackState.clear();
        instanceToPriority.clear();
        nextInstanceId = 1;
        vfLogDebug("[ScriptingAdapter] All scripts unloaded, instance counter reset");
    }

    bool ScriptingAdapter::isScriptLoaded(uint64_t instanceId) const
    {
        return instanceToClassName.find(instanceId) != instanceToClassName.end();
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

    void ScriptingAdapter::startDebugServer(int port)
    {
        if (!initialized || !debugServer || !interpreter)
        {
            return;
        }
        debugServer->start(interpreter.get(), port);
    }

    void ScriptingAdapter::stopDebugServer()
    {
        if (debugServer)
        {
            debugServer->stop();
        }
    }

    bool ScriptingAdapter::isDebuggerActive() const
    {
        return debugServer && debugServer->isActive();
    }

    void ScriptingAdapter::registerPluginNativeFunction(const std::string& name, std::any function)
    {
        if (!interpreter)
        {
            vfLogError("[ScriptingAdapter] Cannot register native function '{}': interpreter not initialized", name);
            return;
        }

        // Engine-plugin C-ABI form: wrap the {MTypeNativeFn, userData} pair in
        // mType's plugin host trampoline (per-call arena, error rethrow), exactly
        // like mType's own PluginLoader does for standalone script plugins.
        if (auto* cAbi = std::any_cast<std::pair<MTypeNativeFn, void*>>(&function))
        {
            auto binding = std::make_unique<::plugin::PluginNativeBinding>();
            binding->fn = cAbi->first;
            binding->userData = cAbi->second;
            binding->owner = nullptr;  // engine-managed, not an mType PluginHandle
            binding->name = name;

            ::services::NativeFunction delegate{};
            delegate.userData = binding.get();
            delegate.invoke = [](void* u, environment::NativeContext& nc,
                                 std::span<const value::Value> args) -> value::Value
            {
                return ::plugin::pluginNativeTrampoline(u, nc, args);
            };

            interpreter->registerNativeFunction(name, std::move(delegate));
            pluginNativeBindings[name] = std::move(binding);
            vfLogDebug("[ScriptingAdapter] Registered plugin native function (C ABI): {}", name);
            return;
        }

        // Engine-internal form: a ready-made NativeDelegate.
        try
        {
            auto nativeFunc = std::any_cast<::services::NativeFunction>(function);
            interpreter->registerNativeFunction(name, std::move(nativeFunc));
            vfLogDebug("[ScriptingAdapter] Registered plugin native function: {}", name);
        }
        catch (const std::bad_any_cast&)
        {
            vfLogError("[ScriptingAdapter] Failed to register '{}': invalid function type", name);
        }
    }

    void ScriptingAdapter::unregisterPluginNativeFunction(const std::string& name)
    {
        if (interpreter)
        {
            auto env = interpreter->getEnvironment();
            auto registry = env ? env->getNativeRegistry() : nullptr;
            if (registry && registry->unregisterNativeFunction(name))
            {
                vfLogDebug("[ScriptingAdapter] Unregistered plugin native function: {}", name);
            }
        }
        // Drop the owned binding last — the registry entry pointing at it is gone.
        pluginNativeBindings.erase(name);
    }

    std::string ScriptingAdapter::getInstanceState(uint64_t instanceId)
    {
        auto objIt = instanceToObject.find(instanceId);
        if (objIt == instanceToObject.end()) return "{}";

        try
        {
            auto& instanceValue = std::any_cast<value::Value&>(objIt->second);
            auto env = interpreter->getEnvironment();
            json::JsonSerializer serializer(env);
            return serializer.serialize(instanceValue);
        }
        catch (const std::exception& e)
        {
            vfLogError("[ScriptingAdapter] getInstanceState failed for {}: {}", instanceId, e.what());
            return "{}";
        }
    }

    bool ScriptingAdapter::setInstanceState(uint64_t instanceId, const std::string& jsonState)
    {
        auto objIt = instanceToObject.find(instanceId);
        if (objIt == instanceToObject.end()) return false;

        auto classIt = instanceToClassName.find(instanceId);
        if (classIt == instanceToClassName.end()) return false;

        try
        {
            auto env = interpreter->getEnvironment();
            json::JsonDeserializer deserializer(env);
            value::Value restored = deserializer.deserializeAs(jsonState, classIt->second);

            // Copy fields from deserialized value to the live instance
            if (value::isObject(restored))
            {
                const auto& restoredObj = value::asObject(restored);
                auto& liveValue = std::any_cast<value::Value&>(objIt->second);
                if (value::isObject(liveValue))
                {
                    const auto& liveObj = value::asObject(liveValue);
                    // mType API: getAllFields() returns vector<pair> by value
                    // (replaces the old getAllFieldValues() which returned the
                    // internal map by reference — type changed, copy is intentional).
                    const auto restoredFields = restoredObj->getAllFields();
                    for (const auto& [fieldName, fieldValue] : restoredFields)
                    {
                        liveObj->setField(fieldName, fieldValue);
                    }
                    return true;
                }
            }
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("[ScriptingAdapter] setInstanceState failed for {}: {}", instanceId, e.what());
            return false;
        }
    }

    bool ScriptingAdapter::isSaveableInstance(uint64_t instanceId) const
    {
        auto classIt = instanceToClassName.find(instanceId);
        if (classIt == instanceToClassName.end()) return false;

        try
        {
            auto env = interpreter->getEnvironment();
            auto classDef = env->findClass(classIt->second);
            if (classDef)
            {
                return classDef->hasAnnotation("Saveable");
            }
        }
        catch (const std::exception&) {}
        return false;
    }

    std::vector<uint64_t> ScriptingAdapter::getAllInstanceIds() const
    {
        std::vector<uint64_t> ids;
        ids.reserve(instanceToObject.size());
        for (const auto& [id, obj] : instanceToObject)
        {
            ids.push_back(id);
        }
        return ids;
    }

    ::services::EntityHandle ScriptingAdapter::getInstanceEntity(uint64_t instanceId) const
    {
        auto it = instanceToEntity.find(instanceId);
        if (it != instanceToEntity.end()) return it->second;
        return ::services::EntityHandle::invalid();
    }

    std::string ScriptingAdapter::getInstanceClassName(uint64_t instanceId) const
    {
        auto it = instanceToClassName.find(instanceId);
        if (it != instanceToClassName.end()) return it->second;
        return "";
    }

    std::string ScriptingAdapter::getInstanceScriptPath(uint64_t instanceId) const
    {
        auto it = instanceToClassName.find(instanceId);
        if (it == instanceToClassName.end()) return "";

        // Reverse lookup: find path from className
        for (const auto& [path, className] : pathToClassName)
        {
            if (className == it->second) return path;
        }
        return "";
    }

    void ScriptingAdapter::setError(services::ScriptError::Type type, const std::string& message,
                                    const std::string& file, int line)
    {
        lastError = services::ScriptError{type, message, file, line, 0};
    }

    std::string ScriptingAdapter::extractClassName(const std::string& scriptPath)
    {
        std::ifstream file(scriptPath);
        if (!file.is_open()) return "";

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Pattern: @Script followed by optional whitespace/newlines, then class ClassName
        std::regex scriptAnnotationPattern(R"(@Script\s+(?:public\s+)?class\s+(\w+)\b)");
        std::smatch match;

        if (std::regex_search(content, match, scriptAnnotationPattern))
            return match[1].str();

        // Fallback for backwards compatibility
        std::regex anyClassPattern(R"(\bclass\s+(\w+)\b)");
        if (std::regex_search(content, match, anyClassPattern))
            return match[1].str();

        return "";
    }
}
