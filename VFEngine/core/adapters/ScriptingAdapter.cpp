#include "ScriptingAdapter.hpp"
#include "NativeAPIRegistry.hpp"
#include <services/ScriptInterpreter.hpp>
#include <value/ValueType.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <regex>

// Include editor logger for console output
#include "print/EditorLogger.hpp"

namespace core {

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

            // Create and initialize native API registry
            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
            apiRegistry->registerEngineAPIs();

            initialized = true;
            spdlog::info("[ScriptingAdapter] Initialized mType scripting system");
            return true;
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to initialize scripting system: ") + e.what());
            vfLogError("[Script] Init failed: {}", e.what());
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

        apiRegistry.reset();
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

            // Check if this script class is already registered (avoid re-parsing)
            std::string className;
            auto pathIt = pathToClassName.find(scriptPath);
            if (pathIt != pathToClassName.end()) {
                // Class already registered, reuse it
                className = pathIt->second;
                spdlog::debug("[ScriptingAdapter] Reusing already registered class: {}", className);
            } else {
                // First time loading this script, parse and register
                spdlog::debug("[ScriptingAdapter] Calling parseAndRegisterClasses...");
                interpreter->parseAndRegisterClasses(fullPath);
                spdlog::debug("[ScriptingAdapter] parseAndRegisterClasses completed successfully");

                // Extract class name from script
                className = extractClassName(fullPath);
                if (className.empty()) {
                    setError(services::ScriptError::Type::Compile,
                             "Could not find class definition in script", scriptPath);
                    return std::nullopt;
                }
                spdlog::debug("[ScriptingAdapter] Extracted class name: {}", className);
            }

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
            vfLogError("[Script] Compile error in '{}': {}", scriptPath, e.what());
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
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);

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
            vfLogError("[Script] onStart failed: {}", e.what());
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
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
                NativeAPIRegistry::setCurrentDeltaTime(deltaTime);

                // Get the script instance and call onUpdate with deltaTime argument
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onUpdate", {value::Value(deltaTime)});
            }
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            vfLogError("[Script] onUpdate failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId) {
        if (!isScriptLoaded(instanceId)) {
            return;
        }

        try {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end()) {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);

                // Get the script instance and call onDestroy
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onDestroy", {});

                spdlog::debug("[ScriptingAdapter] Called onDestroy for instance {}", instanceId);
            }
        }
        catch (const std::exception& e) {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            vfLogError("[Script] onDestroy failed: {}", e.what());
        }
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

}
