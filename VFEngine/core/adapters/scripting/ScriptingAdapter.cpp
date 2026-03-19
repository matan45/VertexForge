// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <project/ProjectBuilder.hpp>
#include <project/ProjectConfigParser.hpp>

#include "ScriptingAdapter.hpp"
#include "ScriptUIEventBridge.hpp"
#include "ScriptPhysicsEventBridge.hpp"
#include "ScriptAnimationEventBridge.hpp"
#include "ScriptSocketEventBridge.hpp"
#include "ScriptVFXEventBridge.hpp"
#include "ScriptNavigationEventBridge.hpp"
#include "ScriptInputActionEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "CoroutineManager.hpp"
#include "ScriptCommunicationManager.hpp"
#include "../api/CoroutineAPI.hpp"
#include "../api/ScriptCommunicationAPI.hpp"
#include <runtime/EventLoop.hpp>
#include <vm/runtime/VirtualMachine.hpp>
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

            communicationManager = std::make_unique<ScriptCommunicationManager>(
                interpreter.get(), instanceToClassName, instanceToEntity, instanceToObject);

            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
            api::CoroutineAPI::setCoroutineManager(coroutineManager.get());
            api::ScriptCommunicationAPI::setManager(communicationManager.get());
            apiRegistry->registerEngineAPIs();

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

            physicsEventBridge->subscribeAll();
            uiEventBridge->subscribeAll();
            animationEventBridge->subscribeAll();
            socketEventBridge->subscribeAll();
            vfxEventBridge->subscribeAll();
            navigationEventBridge->subscribeAll();
            inputActionEventBridge->subscribeAll();

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

        apiRegistry.reset();
        interpreter.reset();
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
            static constexpr std::array<const char*, 15> kCheckedInterfaces = {
                "ICollisionListener", "ITriggerListener",
                "IUIButtonListener", "IUITextInputListener", "IUICheckboxListener",
                "IUIDropdownListener", "IUITabsListener", "IUISliderListener",
                "IUIProgressBarListener", "IUIDragDropListener",
                "IAnimationEventListener",
                "ISocketAttachmentListener",
                "IVFXEventListener",
                "INavigationEventListener",
                "IInputActionListener"
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
        vfLogInfo("[ScriptingAdapter] All scripts unloaded, instance counter reset");
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

    void ScriptingAdapter::registerPluginNativeFunction(const std::string& name, std::any function)
    {
        if (!interpreter)
        {
            vfLogError("[ScriptingAdapter] Cannot register native function '{}': interpreter not initialized", name);
            return;
        }

        try
        {
            auto nativeFunc = std::any_cast<::services::NativeFunction>(function);
            interpreter->registerNativeFunction(name, std::move(nativeFunc));
            vfLogInfo("[ScriptingAdapter] Registered plugin native function: {}", name);
        }
        catch (const std::bad_any_cast&)
        {
            vfLogError("[ScriptingAdapter] Failed to register '{}': invalid function type", name);
        }
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
