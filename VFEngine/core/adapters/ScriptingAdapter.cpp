#include "ScriptingAdapter.hpp"
#include <services/ScriptInterpreter.hpp>
#include <value/ValueType.hpp>
#include <runtimeTypes/klass/ObjectInstance.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <regex>

// Include event dispatcher for Entity API callbacks
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/SceneEvents.hpp"

// Include editor logger for console output
#include "print/EditorLogger.hpp"

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
        instanceToObject.clear();
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

            spdlog::debug("[ScriptingAdapter] Loading script: {}", fullPath);
            spdlog::debug("[ScriptingAdapter] Calling parseAndRegisterClasses...");

            interpreter->parseAndRegisterClasses(fullPath);

            spdlog::debug("[ScriptingAdapter] parseAndRegisterClasses completed successfully");

            // Extract class name from script
            std::string className = extractClassName(fullPath);
            if (className.empty()) {
                setError(services::ScriptError::Type::Compile,
                         "Could not find class definition in script", scriptPath);
                return std::nullopt;
            }

            spdlog::debug("[ScriptingAdapter] Extracted class name: {}", className);

            // Create script instance
            spdlog::debug("[ScriptingAdapter] Calling createObject for class: {}", className);
            auto instance = interpreter->createObject(className);
            spdlog::debug("[ScriptingAdapter] createObject completed successfully");

            // Assign instance ID
            uint64_t instanceId = nextInstanceId++;

            // Store mappings
            instanceToClassName[instanceId] = className;
            instanceToEntity[instanceId] = entity;
            instanceToObject[instanceId] = std::any(instance);  // Store as type-erased any
            pathToClassName[scriptPath] = className;

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
            instanceToObject.erase(instanceId);
        }
    }

    bool ScriptingAdapter::isScriptLoaded(uint64_t instanceId) const {
        return instanceToClassName.find(instanceId) != instanceToClassName.end();
    }

    void ScriptingAdapter::callOnStart(uint64_t instanceId) {
        if (!isScriptLoaded(instanceId)) {
            spdlog::warn("[ScriptingAdapter] callOnStart: script {} not loaded", instanceId);
            return;
        }

        try {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end()) {
                // Set current entity for callbacks
                currentCallbackEntity = instanceToEntity[instanceId];

                // Get the script instance and call onStart
                spdlog::info("[ScriptingAdapter] Calling interpreter->callMethod for onStart (instance {})", instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onStart", {});

                spdlog::info("[ScriptingAdapter] onStart completed successfully for instance {}", instanceId);
            } else {
                spdlog::warn("[ScriptingAdapter] callOnStart: instance {} not found in instanceToObject", instanceId);
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
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end()) {
                // Set current context for callbacks
                currentCallbackEntity = instanceToEntity[instanceId];
                currentDeltaTime = deltaTime;

                // Get the script instance and call onUpdate with deltaTime argument
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onUpdate", {value::Value(deltaTime)});
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
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end()) {
                currentCallbackEntity = instanceToEntity[instanceId];

                // Get the script instance and call onDestroy
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onDestroy", {});

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

        // Look for "@Script" annotation followed by a class definition
        // Pattern matches: @Script followed by optional whitespace/newlines, then class ClassName
        std::regex scriptAnnotationPattern(R"(@Script\s+(?:public\s+)?class\s+(\w+)\b)");
        std::smatch match;

        if (std::regex_search(content, match, scriptAnnotationPattern)) {
            return match[1].str();
        }

        // Fallback: look for any class definition (for backwards compatibility)
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

    // Helper function to extract string from value::Value
    static std::string extractString(const value::Value& val) {
        if (std::holds_alternative<std::string>(val)) {
            return std::get<std::string>(val);
        }
        if (std::holds_alternative<value::InternedString>(val)) {
            return std::get<value::InternedString>(val).getString();
        }
        // Check for boxed String object
        if (std::holds_alternative<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val)) {
            auto obj = std::get<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val);
            if (obj && obj->getTypeName() == "String") {
                auto fieldVal = obj->getFieldValue("value");
                if (std::holds_alternative<std::string>(fieldVal)) {
                    return std::get<std::string>(fieldVal);
                }
                if (std::holds_alternative<value::InternedString>(fieldVal)) {
                    return std::get<value::InternedString>(fieldVal).getString();
                }
            }
        }
        return "";
    }

    void ScriptingAdapter::registerLogClass() {
        // Register global native functions that Log.mt will wrap
        interpreter->registerNativeFunction("_native_log_info",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty()) {
                    std::string message = extractString(args[0]);
                    if (!message.empty()) {
                        vfLogInfo("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_log_warn",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty()) {
                    std::string message = extractString(args[0]);
                    if (!message.empty()) {
                        vfLogWarning("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_log_error",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (!args.empty()) {
                    std::string message = extractString(args[0]);
                    if (!message.empty()) {
                        vfLogError("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });
    }

    void ScriptingAdapter::registerTimeClass() {
        // Register global native functions that Time.mt will wrap
        interpreter->registerNativeFunction("_native_time_getDeltaTime",
            [](const std::vector<value::Value>& args) -> value::Value {
                return value::Value(currentDeltaTime);
            });

        interpreter->registerNativeFunction("_native_time_getTime",
            [](const std::vector<value::Value>& args) -> value::Value {
                return value::Value(0.0f);  // TODO: Implement actual elapsed time
            });
    }

    void ScriptingAdapter::registerEntityClass() {
        // Entity native functions will be added later when full Entity API is needed
        // For now, scripts use the entity ID set by the engine
    }

}
