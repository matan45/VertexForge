#include "ScriptingAdapter.hpp"
#include <services/ScriptInterpreter.hpp>
#include <value/ValueType.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <regex>

// Include event dispatcher for Entity API callbacks
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/SceneEvents.hpp"

namespace core {

    // Static members for callback context
    services::EntityHandle ScriptingAdapter::currentCallbackEntity = services::EntityHandle::invalid();
    float ScriptingAdapter::currentDeltaTime = 0.0f;

    ScriptingAdapter::ScriptingAdapter() = default;

    ScriptingAdapter::~ScriptingAdapter() {
        cleanUp();
    }

    bool ScriptingAdapter::init() {
        if (initialized) {
            return true;
        }

        try {
            interpreter = std::make_unique<::services::ScriptInterpreter>();

            // Register native engine APIs
            registerEngineAPIs();

            initialized = true;
            spdlog::info("[ScriptingAdapter] Initialized mType scripting system");
            return true;
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to initialize scripting system: ") + e.what());
            spdlog::error("[ScriptingAdapter] Init failed: {}", e.what());
            return false;
        }
    }

    void ScriptingAdapter::cleanUp() {
        if (!initialized) {
            return;
        }

        // Clean up all script instances
        instanceToClassName.clear();
        instanceToEntity.clear();
        pathToClassName.clear();

        interpreter.reset();
        initialized = false;

        spdlog::info("[ScriptingAdapter] Cleaned up scripting system");
    }

    bool ScriptingAdapter::isInitialized() const {
        return initialized;
    }

    std::optional<services::ScriptInstanceInfo> ScriptingAdapter::loadScript(
        const std::string& scriptPath,
        services::EntityHandle entity)
    {
        if (!initialized) {
            setError(services::ScriptError::Type::Runtime, "Scripting system not initialized");
            return std::nullopt;
        }

        try {
            // Parse and register the script class
            std::string fullPath = scriptLibraryPath.empty() ? scriptPath :
                                   scriptLibraryPath + "/" + scriptPath;

            interpreter->parseAndRegisterClasses(fullPath);

            // Extract class name from script
            std::string className = extractClassName(fullPath);
            if (className.empty()) {
                setError(services::ScriptError::Type::Compile,
                         "Could not find class definition in script", scriptPath);
                return std::nullopt;
            }

            // Create script instance
            auto instance = interpreter->createObject(className);

            // Assign instance ID
            uint64_t instanceId = nextInstanceId++;

            // Store mappings
            instanceToClassName[instanceId] = className;
            instanceToEntity[instanceId] = entity;
            pathToClassName[scriptPath] = className;

            // Set the entity field on the script instance
            // (This requires the Entity native class to be registered)
            // interpreter->setField(instance, "entity", createEntityValue(entity));

            // Build result
            services::ScriptInstanceInfo info;
            info.instanceId = instanceId;
            info.className = className;
            info.scriptPath = scriptPath;

            // Check which lifecycle methods exist
            // TODO: Implement method existence check via mType API
            info.hasOnStart = true;   // Assume present for now
            info.hasOnUpdate = true;
            info.hasOnDestroy = true;

            spdlog::info("[ScriptingAdapter] Loaded script '{}' as class '{}' (instanceId={})",
                         scriptPath, className, instanceId);

            return info;
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Compile, e.what(), scriptPath);
            spdlog::error("[ScriptingAdapter] Failed to load script '{}': {}", scriptPath, e.what());
            return std::nullopt;
        }
    }

    void ScriptingAdapter::unloadScript(uint64_t instanceId) {
        auto it = instanceToClassName.find(instanceId);
        if (it != instanceToClassName.end()) {
            spdlog::debug("[ScriptingAdapter] Unloading script instance {}", instanceId);
            instanceToClassName.erase(it);
            instanceToEntity.erase(instanceId);
        }
    }

    bool ScriptingAdapter::isScriptLoaded(uint64_t instanceId) const {
        return instanceToClassName.find(instanceId) != instanceToClassName.end();
    }

    void ScriptingAdapter::callOnStart(uint64_t instanceId) {
        if (!isScriptLoaded(instanceId)) {
            return;
        }

        try {
            auto it = instanceToClassName.find(instanceId);
            if (it != instanceToClassName.end()) {
                // Set current entity for callbacks
                currentCallbackEntity = instanceToEntity[instanceId];

                // TODO: Get the actual object instance and call onStart
                // interpreter->callMethod(instance, "onStart", {});

                spdlog::debug("[ScriptingAdapter] Called onStart for instance {}", instanceId);
            }
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onStart failed: ") + e.what());
            spdlog::error("[ScriptingAdapter] onStart failed for instance {}: {}",
                          instanceId, e.what());
        }
    }

    void ScriptingAdapter::callOnUpdate(uint64_t instanceId, float deltaTime) {
        if (!isScriptLoaded(instanceId)) {
            return;
        }

        try {
            auto it = instanceToClassName.find(instanceId);
            if (it != instanceToClassName.end()) {
                // Set current context for callbacks
                currentCallbackEntity = instanceToEntity[instanceId];
                currentDeltaTime = deltaTime;

                // TODO: Get the actual object instance and call onUpdate
                // interpreter->callMethod(instance, "onUpdate", {value::Value(deltaTime)});
            }
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            spdlog::error("[ScriptingAdapter] onUpdate failed for instance {}: {}",
                          instanceId, e.what());
        }
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId) {
        if (!isScriptLoaded(instanceId)) {
            return;
        }

        try {
            auto it = instanceToClassName.find(instanceId);
            if (it != instanceToClassName.end()) {
                currentCallbackEntity = instanceToEntity[instanceId];

                // TODO: Get the actual object instance and call onDestroy
                // interpreter->callMethod(instance, "onDestroy", {});

                spdlog::debug("[ScriptingAdapter] Called onDestroy for instance {}", instanceId);
            }
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            spdlog::error("[ScriptingAdapter] onDestroy failed for instance {}: {}",
                          instanceId, e.what());
        }
    }

    std::vector<services::ScriptPropertyInfo> ScriptingAdapter::getProperties(uint64_t instanceId) const {
        // TODO: Implement property enumeration via mType reflection
        return {};
    }

    bool ScriptingAdapter::setProperty(uint64_t instanceId, const std::string& name,
                                        const std::any& value) {
        // TODO: Implement property setting via mType API
        return false;
    }

    std::optional<std::any> ScriptingAdapter::getProperty(uint64_t instanceId,
                                                           const std::string& name) const {
        // TODO: Implement property getting via mType API
        return std::nullopt;
    }

    std::vector<services::ScriptMethodInfo> ScriptingAdapter::getMethods(uint64_t instanceId) const {
        // TODO: Implement method enumeration via mType reflection
        return {};
    }

    std::optional<std::any> ScriptingAdapter::callMethod(uint64_t instanceId,
                                                          const std::string& methodName,
                                                          const std::vector<std::any>& args) {
        // TODO: Implement method calling via mType API
        return std::nullopt;
    }

    std::optional<services::ScriptError> ScriptingAdapter::getLastError() const {
        return lastError;
    }

    void ScriptingAdapter::clearError() {
        lastError = std::nullopt;
    }

    void ScriptingAdapter::setScriptLibraryPath(const std::string& path) {
        scriptLibraryPath = path;
        spdlog::info("[ScriptingAdapter] Script library path set to: {}", path);
    }

    void ScriptingAdapter::setError(services::ScriptError::Type type, const std::string& message,
                                     const std::string& file, int line) {
        lastError = services::ScriptError{type, message, file, line, 0};
    }

    std::string ScriptingAdapter::extractClassName(const std::string& scriptPath) {
        // Read the script file and extract the class name
        std::ifstream file(scriptPath);
        if (!file.is_open()) {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Look for "class ClassName extends EngineScript" pattern
        std::regex classPattern(R"(\bclass\s+(\w+)\s+extends\s+EngineScript\b)");
        std::smatch match;

        if (std::regex_search(content, match, classPattern)) {
            return match[1].str();
        }

        // Fallback: look for any class definition
        std::regex anyClassPattern(R"(\bclass\s+(\w+)\b)");
        if (std::regex_search(content, match, anyClassPattern)) {
            return match[1].str();
        }

        return "";
    }

    void ScriptingAdapter::registerEngineAPIs() {
        registerLogClass();
        registerTimeClass();
        registerEntityClass();

        spdlog::debug("[ScriptingAdapter] Registered native engine APIs");
    }

    void ScriptingAdapter::registerLogClass() {
        interpreter->registerNativeClass("Log");

        // Log.info(string message)
        interpreter->registerNativeMethod("Log", "info",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                    spdlog::info("[Script] {}", std::get<std::string>(args[0]));
                }
                return value::Value(std::monostate{});
            }, true);

        // Log.warn(string message)
        interpreter->registerNativeMethod("Log", "warn",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                    spdlog::warn("[Script] {}", std::get<std::string>(args[0]));
                }
                return value::Value(std::monostate{});
            }, true);

        // Log.error(string message)
        interpreter->registerNativeMethod("Log", "error",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                    spdlog::error("[Script] {}", std::get<std::string>(args[0]));
                }
                return value::Value(std::monostate{});
            }, true);
    }

    void ScriptingAdapter::registerTimeClass() {
        interpreter->registerNativeClass("Time");

        // Time.getDeltaTime() : float
        interpreter->registerNativeMethod("Time", "getDeltaTime",
            [](const std::vector<value::Value>& args) -> value::Value {
                return value::Value(currentDeltaTime);
            }, true);

        // Time.getTime() : float
        // TODO: Get actual elapsed time from engine Timer
        interpreter->registerNativeMethod("Time", "getTime",
            [](const std::vector<value::Value>& args) -> value::Value {
                return value::Value(0.0f);  // TODO: Implement
            }, true);
    }

    void ScriptingAdapter::registerEntityClass() {
        interpreter->registerNativeClass("Entity");

        // Entity._handleId field (internal, stores the EntityHandle id)
        interpreter->registerNativeField("Entity", "_handleId", value::Value(int64_t(0)), false);

        // Entity.getName() : string
        interpreter->registerNativeMethod("Entity", "getName",
            [](const std::vector<value::Value>& args) -> value::Value {
                // args[0] is 'this' - the Entity object
                // TODO: Extract handle and query name via EventDispatcher
                return value::Value(std::string("Entity"));
            }, false);

        // Entity.getPosition() : Vec3f
        // TODO: Implement when Vec3f native integration is ready

        // Entity.setPosition(Vec3f pos) : void
        // TODO: Implement when Vec3f native integration is ready
    }

}
