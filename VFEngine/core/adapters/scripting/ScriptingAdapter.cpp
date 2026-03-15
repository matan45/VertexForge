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
#include "NativeAPIRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <array>


#include "print/Log.hpp"
namespace core
{
    namespace
    {
        void callScriptMethod(::services::ScriptInterpreter* interpreter,
                              std::unordered_map<uint64_t, std::any>& instanceToObject,
                              std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
                              uint64_t instanceId, const char* methodName,
                              const std::vector<value::Value>& args)
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) return;

            NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
            auto& instance = std::any_cast<value::Value&>(objIt->second);
            interpreter->callMethod(instance, methodName, args);
        }
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

            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
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

            physicsEventBridge->subscribeAll();
            uiEventBridge->subscribeAll();
            animationEventBridge->subscribeAll();
            socketEventBridge->subscribeAll();
            vfxEventBridge->subscribeAll();

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
            static constexpr std::array<const char*, 13> kCheckedInterfaces = {
                "ICollisionListener", "ITriggerListener",
                "IUIButtonListener", "IUITextInputListener", "IUICheckboxListener",
                "IUIDropdownListener", "IUITabsListener", "IUISliderListener",
                "IUIProgressBarListener", "IUIDragDropListener",
                "IAnimationEventListener",
                "ISocketAttachmentListener",
                "IVFXEventListener"
            };

            std::unordered_set<std::string> interfaces;
            for (const auto* iface : kCheckedInterfaces)
            {
                if (interpreter->classImplementsInterface(className, iface))
                    interfaces.insert(iface);
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
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onStart", {});
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
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onUpdate", {value::Value(deltaTime)});
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            vfLogError("[Script] onUpdate failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnFixedUpdate(uint64_t instanceId, float fixedDeltaTime)
    {
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onFixedUpdate", {value::Value(fixedDeltaTime)});
        }
        catch (const std::exception&)
        {
            // Silently ignore if script does not define onFixedUpdate
        }
    }

    void ScriptingAdapter::callOnLateUpdate(uint64_t instanceId, float deltaTime)
    {
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onLateUpdate", {value::Value(deltaTime)});
        }
        catch (const std::exception&)
        {
            // Silently ignore if script does not define onLateUpdate
        }
    }

    void ScriptingAdapter::callOnEnable(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onEnable", {});
        }
        catch (const std::exception&)
        {
            // Silently ignore if script does not define onEnable
        }
    }

    void ScriptingAdapter::callOnDisable(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onDisable", {});
        }
        catch (const std::exception&)
        {
            // Silently ignore if script does not define onDisable
        }
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onDestroy", {});
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            vfLogError("[Script] onDestroy failed: {}", e.what());
        }
    }

    std::string ScriptingAdapter::callMethodWithReturn(uint64_t instanceId, const std::string& methodName,
                                                       const std::vector<std::any>& args)
    {
        if (!isScriptLoaded(instanceId)) return "";

        auto objIt = instanceToObject.find(instanceId);
        if (objIt == instanceToObject.end()) return "";

        try
        {
            NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
            auto& instance = std::any_cast<value::Value&>(objIt->second);

            // Convert std::any args to value::Value args
            std::vector<value::Value> valueArgs;
            for (const auto& arg : args)
            {
                if (arg.type() == typeid(float))
                    valueArgs.emplace_back(static_cast<double>(std::any_cast<float>(arg)));
                else if (arg.type() == typeid(double))
                    valueArgs.emplace_back(std::any_cast<double>(arg));
                else if (arg.type() == typeid(int))
                    valueArgs.emplace_back(static_cast<int64_t>(std::any_cast<int>(arg)));
                else if (arg.type() == typeid(int64_t))
                    valueArgs.emplace_back(std::any_cast<int64_t>(arg));
                else if (arg.type() == typeid(bool))
                    valueArgs.emplace_back(std::any_cast<bool>(arg));
                else if (arg.type() == typeid(std::string))
                    valueArgs.emplace_back(std::any_cast<std::string>(arg));
            }

            auto result = interpreter->callMethod(instance, methodName, valueArgs);

            if (std::holds_alternative<std::string>(result))
                return std::get<std::string>(result);
            if (std::holds_alternative<bool>(result))
                return std::get<bool>(result) ? "success" : "failure";

            return "";
        }
        catch (const std::exception& e)
        {
            vfLogError("[Script] {} failed: {}", methodName, e.what());
            return "";
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
            callOnStart(instanceId);
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
