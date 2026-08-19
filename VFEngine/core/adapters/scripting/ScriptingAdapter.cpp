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
#include "ScriptOceanEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "CoroutineManager.hpp"
#include "ScriptCommunicationManager.hpp"
#include "ScriptDebugServer.hpp"
#include "../api/CoroutineAPI.hpp"
#include "../api/ScriptCommunicationAPI.hpp"
#include "../api/PluginComponentAPI.hpp"
#include "../../../plugin/core/PluginContextImpl.hpp"
#include "../../../plugin/core/PluginEventBus.hpp"
#include <runtime/EventLoop.hpp>
#include <vm/runtime/VirtualMachine.hpp>
#include <environment/Environment.hpp>
#include <environment/registry/ClassDefinition.hpp>
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
    namespace
    {
        // Backstop for the plugin-event bridge: pumpPluginEvents() only runs while scripts
        // are updating, so a plugin publishing in Edit mode (or while game time is frozen)
        // would otherwise grow the queue without bound. Deep enough that a normal frame's
        // burst never trips it.
        constexpr size_t kMaxPendingPluginEvents = 4096;
    }

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

            // Bridge PluginEventBus -> mType ScriptEvent, lazily and per event name: an engine
            // plugin publishes on the bus (PluginContextImpl::publishEvent) and its handlers run
            // INLINE on the publisher's thread, which is never the thread that owns the
            // interpreter. So the bus handler only queues; pumpPluginEvents() drains it at the
            // top of the script update, on the script thread.
            communicationManager->setOnFirstListen([this](const std::string& eventName) {
                {
                    std::lock_guard<std::mutex> lock(pluginEventMutex);
                    if (!bridgedPluginEvents.insert(eventName).second)
                        return;  // already bridged (the listener list emptied and refilled)
                }

                // Subscribe OUTSIDE pluginEventMutex. PluginEventBus::publish takes the bus
                // lock and then runs this handler, which takes pluginEventMutex; acquiring the
                // two in the opposite order here would be a lock-order inversion.
                auto token = plugin::PluginEventBus::instance().subscribe(
                    eventName,
                    [this, eventName](const nlohmann::json& data) {
                        std::lock_guard<std::mutex> queueLock(pluginEventMutex);
                        if (pendingPluginEvents.size() >= kMaxPendingPluginEvents)
                        {
                            pendingPluginEvents.pop_front();
                        }
                        pendingPluginEvents.emplace_back(eventName, data.dump());
                    });

                std::lock_guard<std::mutex> lock(pluginEventMutex);
                pluginEventTokens.push_back(token);
            });

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

            // PluginManager invokes this during teardown, before unloading the plugin DLLs, so the
            // engine drops its cached bridges (whose std::functions live in the DLLs) while their
            // code is still mapped — otherwise destroying them post-unload faults (shutdown crash).
            plugin::PluginContextImpl::setScriptBindingClearer(
                []() { api::PluginComponentAPI::cleanup(); });

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
            oceanEventBridge = std::make_unique<ScriptOceanEventBridge>(
                interpreter.get(), instanceToInterfaces, instanceToObject, instanceToEntity);

            // Aggregate every bridge's dispatched interfaces into the set
            // loadScript probes. New bridges only have to declare their
            // kRequiredInterfaces — no second list to keep in sync.
            checkedInterfaces.clear();
            auto collectInterfaces = [this](const auto& required)
            {
                for (const char* name : required) checkedInterfaces.insert(name);
            };
            collectInterfaces(ScriptUIEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptPhysicsEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptAnimationEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptSocketEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptVFXEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptNavigationEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptInputActionEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptSceneEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptWeatherEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptDestructionEventBridge::kRequiredInterfaces);
            collectInterfaces(ScriptOceanEventBridge::kRequiredInterfaces);

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
            oceanEventBridge->subscribeAll();

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

        if (oceanEventBridge) oceanEventBridge->unsubscribeAll();
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

        // Drop the plugin-bus bridge before the interpreter goes away: its handlers capture
        // `this`, and PluginEventBus outlives the adapter. Unsubscribe outside the queue lock
        // (the bus takes its own lock and its handler takes ours).
        std::vector<::events::SubscriptionToken> tokensToDrop;
        {
            std::lock_guard<std::mutex> lock(pluginEventMutex);
            tokensToDrop.swap(pluginEventTokens);
            pendingPluginEvents.clear();
            bridgedPluginEvents.clear();
        }
        for (const auto& token : tokensToDrop)
        {
            plugin::PluginEventBus::instance().unsubscribe(token);
        }

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

            // Relative script paths (e.g. a behavior-tree ScriptTask's "game/ai/X.mt")
            // resolve against the manifest's directory (the script source root). Scene
            // ScriptComponents store absolute paths and are unaffected — loadScript falls
            // back to the verbatim path when the joined path doesn't exist.
            scriptLibraryPath = std::filesystem::path(manifestPath).parent_path().string();

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

        // No `compiled` precondition: loading a prebuilt scripts.mtcLib is the ONLY
        // way a shipped Runtime gets script classes (nothing outside the editor ever
        // dispatches BuildScriptsCommand). A successful load is what makes the VM
        // "compiled" — see the flag set after loadFromProgram below.

        try
        {
            // Mirror buildScripts: relative script paths resolve against the script
            // source root (the manifest's directory) even when loading a prebuilt lib
            // without a fresh build (e.g. on project open).
            scriptLibraryPath = std::filesystem::path(manifestPath).parent_path().string();

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

            // The program is resident: loadScript/createObject can now resolve script
            // classes, so the VM is compiled whether the bytecode came from a build in
            // this process or from a prebuilt library on disk.
            compiled = true;

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

            // VK-1458: bind entity identity onto Behaviour-derived instances.
            // Behaviour's accessors fall back to the ambient Entity::self()
            // when unbound, but the injected field survives cross-script
            // direct method calls and coroutine resumption, where the ambient
            // context belongs to someone else (or nobody).
            injectBehaviourEntityId(instanceId);

            // Cache implemented interfaces for the event bridges. The probe
            // set is the union of every bridge's kRequiredInterfaces,
            // aggregated in init() — never a hand-maintained list here.
            std::unordered_set<std::string> interfaces;
            for (const auto& iface : checkedInterfaces)
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

        // No listeners are left to receive them, and nothing will drain the queue until the
        // next play session starts.
        {
            std::lock_guard<std::mutex> lock(pluginEventMutex);
            pendingPluginEvents.clear();
        }

        vfLogDebug("[ScriptingAdapter] All scripts unloaded, instance counter reset");
    }

    void ScriptingAdapter::pumpPluginEvents()
    {
        if (!communicationManager) return;

        std::deque<std::pair<std::string, std::string>> drained;
        {
            std::lock_guard<std::mutex> lock(pluginEventMutex);
            if (pendingPluginEvents.empty()) return;
            drained.swap(pendingPluginEvents);
        }

        // Dispatch outside the lock: a listener may call ScriptEvent.listenJson, which takes
        // the same mutex through the onFirstListen hook.
        for (const auto& [eventName, jsonPayload] : drained)
        {
            communicationManager->emitPluginEvent(eventName, jsonPayload);
        }
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

                    // The copy above may have restored a STALE persisted
                    // vfEntityId (the serializer round-trips every field);
                    // re-inject the live entity binding for Behaviours.
                    injectBehaviourEntityId(instanceId);
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

    bool ScriptingAdapter::classExtendsBehaviour(const std::string& className) const
    {
        if (!interpreter) return false;

        try
        {
            auto env = interpreter->getEnvironment();
            if (!env) return false;

            auto classDef = env->findClass(className);
            // Walk the parent chain (bounded — mirrors mType's own
            // MAX_INHERITANCE_DEPTH) looking for the OOP base class.
            int depth = 0;
            auto current = classDef ? classDef->getParentClass() : nullptr;
            while (current && depth < 32)
            {
                if (current->getName() == "Behaviour") return true;
                current = current->getParentClass();
                ++depth;
            }
        }
        catch (const std::exception&) {}
        return false;
    }

    void ScriptingAdapter::injectBehaviourEntityId(uint64_t instanceId)
    {
        auto classIt = instanceToClassName.find(instanceId);
        auto objIt = instanceToObject.find(instanceId);
        auto entityIt = instanceToEntity.find(instanceId);
        if (classIt == instanceToClassName.end() ||
            objIt == instanceToObject.end() ||
            entityIt == instanceToEntity.end())
            return;

        if (!classExtendsBehaviour(classIt->second)) return;

        try
        {
            auto& instanceValue = std::any_cast<value::Value&>(objIt->second);
            if (!value::isObject(instanceValue)) return;

            // Field storage is flat per-instance (inherited fields included),
            // so setting the base-class field on the leaf instance works.
            const auto& instanceObj = value::asObject(instanceValue);
            instanceObj->setField(
                "vfEntityId",
                value::Value(static_cast<int64_t>(entityIt->second.id)));

            // Behaviour lazily caches gameObject()/transform() wrappers that
            // embed the entity id; a @Saveable restore round-trips those fields
            // and pins them to the save-time id. Drop the caches so the next
            // gameObject()/transform() rebuilds from the corrected vfEntityId.
            // No-op right after construction (fields already null).
            instanceObj->setField("vfGameObject", value::Value(nullptr));
            instanceObj->setField("vfTransform", value::Value(nullptr));
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[ScriptingAdapter] Behaviour entity injection failed for {}: {}",
                         instanceId, e.what());
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
