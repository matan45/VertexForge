#include "ScriptingAdapter.hpp"
#include <services/ScriptInterpreter.hpp>
#include <value/ValueType.hpp>
#include <value/NativeArray.hpp>
#include <runtimeTypes/klass/ObjectInstance.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <regex>

// Include event dispatcher for Entity API callbacks
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/events/MaterialEvents.hpp"
#include "../../services/events/AudioEvents.hpp"
#include "../../services/events/ScriptingEvents.hpp"
#include "../../services/events/InputEvents.hpp"

// Include ECS for direct component access
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../services/data/EntityConversion.hpp"

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
        registerAudioClass();
        registerInputClass();

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

    // Helper: Extract int64 from value::Value (entity ID)
    static int64_t extractInt64(const value::Value& val) {
        if (std::holds_alternative<int64_t>(val)) {
            return std::get<int64_t>(val);
        }
        return -1;
    }

    // Helper: Extract float from value::Value
    static float extractFloat(const value::Value& val) {
        if (std::holds_alternative<float>(val)) {
            return std::get<float>(val);
        }
        if (std::holds_alternative<int64_t>(val)) {
            return static_cast<float>(std::get<int64_t>(val));
        }
        return 0.0f;
    }

    // Helper: Convert EntityHandle to int64 for scripts
    static int64_t entityToInt(const services::EntityHandle& handle) {
        if (!handle.isValid()) {
            return -1;
        }
        return static_cast<int64_t>(handle.id);
    }

    // Helper: Convert int64 from script to EntityHandle
    static services::EntityHandle intToEntity(int64_t id) {
        if (id < 0) {
            return services::EntityHandle::invalid();
        }
        return services::EntityHandle{ static_cast<uint64_t>(id) };
    }

    // Helper: Map component type string to ComponentTypeId
    static services::ComponentTypeId stringToComponentType(const std::string& type) {
        if (type == "Transform") return services::ComponentTypeId::Transform;
        if (type == "Camera") return services::ComponentTypeId::Camera;
        if (type == "Name") return services::ComponentTypeId::Name;
        if (type == "Parent") return services::ComponentTypeId::Parent;
        if (type == "Children") return services::ComponentTypeId::Children;
        if (type == "WorldTransform") return services::ComponentTypeId::WorldTransform;
        if (type == "IBL") return services::ComponentTypeId::IBL;
        if (type == "Mesh") return services::ComponentTypeId::Mesh;
        if (type == "Light") return services::ComponentTypeId::Light;
        if (type == "Material") return services::ComponentTypeId::Material;
        if (type == "Billboard") return services::ComponentTypeId::Billboard;
        if (type == "AudioSource2D") return services::ComponentTypeId::AudioSource2D;
        if (type == "AudioSource3D") return services::ComponentTypeId::AudioSource3D;
        if (type == "Script") return services::ComponentTypeId::Script;
        return services::ComponentTypeId::Transform; // Default fallback
    }

    // Helper: Map ComponentTypeId to string
    static std::string componentTypeToString(services::ComponentTypeId type) {
        switch (type) {
            case services::ComponentTypeId::Transform: return "Transform";
            case services::ComponentTypeId::Camera: return "Camera";
            case services::ComponentTypeId::Name: return "Name";
            case services::ComponentTypeId::Parent: return "Parent";
            case services::ComponentTypeId::Children: return "Children";
            case services::ComponentTypeId::WorldTransform: return "WorldTransform";
            case services::ComponentTypeId::IBL: return "IBL";
            case services::ComponentTypeId::Mesh: return "Mesh";
            case services::ComponentTypeId::Light: return "Light";
            case services::ComponentTypeId::Material: return "Material";
            case services::ComponentTypeId::Billboard: return "Billboard";
            case services::ComponentTypeId::AudioSource2D: return "AudioSource2D";
            case services::ComponentTypeId::AudioSource3D: return "AudioSource3D";
            case services::ComponentTypeId::Script: return "Script";
            default: return "Unknown";
        }
    }

    void ScriptingAdapter::registerEntityClass() {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_entity_getSelf() -> int64 (current script's entity)
        interpreter->registerNativeFunction("_native_entity_getSelf",
            [](const std::vector<value::Value>& args) -> value::Value {
                return value::Value(entityToInt(currentCallbackEntity));
            });

        // _native_entity_findByName(name) -> int64 (first match, -1 if not found)
        interpreter->registerNativeFunction("_native_entity_findByName",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(-1));
                }
                std::string name = extractString(args[0]);
                if (name.empty()) {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::scene::FindEntitiesByNameQuery query;
                query.name = name;
                auto results = dispatcher.query(query);

                if (!results.empty()) {
                    return value::Value(entityToInt(results[0]));
                }
                return value::Value(static_cast<int64_t>(-1));
            });

        // _native_entity_findAll(name) -> int64[] (all matches)
        interpreter->registerNativeFunction("_native_entity_findAll",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                std::string name = extractString(args[0]);
                if (name.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::FindEntitiesByNameQuery query;
                query.name = name;
                auto results = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(results.size(), value::ValueType::INT);
                for (size_t i = 0; i < results.size(); ++i) {
                    arr->set(i, value::Value(entityToInt(results[i])));
                }
                return value::Value(arr);
            });

        // _native_entity_findWithComponent(type) -> int64[] (entities with component)
        interpreter->registerNativeFunction("_native_entity_findWithComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                std::string typeName = extractString(args[0]);
                if (typeName.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::GetEntitiesWithComponentQuery query;
                query.componentType = stringToComponentType(typeName);
                auto results = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(results.size(), value::ValueType::INT);
                for (size_t i = 0; i < results.size(); ++i) {
                    arr->set(i, value::Value(entityToInt(results[i])));
                }
                return value::Value(arr);
            });

        // _native_entity_isValid(id) -> bool
        interpreter->registerNativeFunction("_native_entity_isValid",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(false);
                }

                services::EntityHandle handle = intToEntity(id);
                events::scene::GetEntityQuery query;
                query.entity = handle;
                auto result = dispatcher.query(query);
                return value::Value(result.has_value());
            });

        // _native_entity_isActive(id) -> bool
        interpreter->registerNativeFunction("_native_entity_isActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(false);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    return value::Value(result->isActive);
                }
                return value::Value(false);
            });

        // _native_entity_setActive(id, active) -> void
        interpreter->registerNativeFunction("_native_entity_setActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                bool active = std::get<bool>(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::SetEntityActiveCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.isActive = active;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_entity_getName(id) -> string
        interpreter->registerNativeFunction("_native_entity_getName",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(std::string(""));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::string(""));
                }

                services::EntityHandle handle = intToEntity(id);
                events::scene::GetEntityQuery query;
                query.entity = handle;
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    return value::Value(result->name);
                }
                return value::Value(std::string(""));
            });

        // _native_entity_setName(id, name) -> void
        interpreter->registerNativeFunction("_native_entity_setName",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                std::string name = extractString(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::SetEntityNameCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.newName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_entity_getPosition(id) -> float[3] (x, y, z)
        interpreter->registerNativeFunction("_native_entity_getPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(0.0f));
                arr->set(1, value::Value(0.0f));
                arr->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    arr->set(0, value::Value(result->position.x));
                    arr->set(1, value::Value(result->position.y));
                    arr->set(2, value::Value(result->position.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setPosition(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                // Get current transform first
                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    return value::Value(std::monostate{});
                }

                // Update position
                services::TransformData newTransform = *currentTransform;
                newTransform.position.x = extractFloat(args[1]);
                newTransform.position.y = extractFloat(args[2]);
                newTransform.position.z = extractFloat(args[3]);

                events::scene::SetTransformCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.transform = newTransform;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_entity_getRotation(id) -> float[3] (euler x, y, z)
        interpreter->registerNativeFunction("_native_entity_getRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(0.0f));
                arr->set(1, value::Value(0.0f));
                arr->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    arr->set(0, value::Value(result->rotation.x));
                    arr->set(1, value::Value(result->rotation.y));
                    arr->set(2, value::Value(result->rotation.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setRotation(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    return value::Value(std::monostate{});
                }

                services::TransformData newTransform = *currentTransform;
                newTransform.rotation.x = extractFloat(args[1]);
                newTransform.rotation.y = extractFloat(args[2]);
                newTransform.rotation.z = extractFloat(args[3]);

                events::scene::SetTransformCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.transform = newTransform;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_entity_getScale(id) -> float[3]
        interpreter->registerNativeFunction("_native_entity_getScale",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(1.0f));
                arr->set(1, value::Value(1.0f));
                arr->set(2, value::Value(1.0f));

                if (args.empty()) {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    arr->set(0, value::Value(result->scale.x));
                    arr->set(1, value::Value(result->scale.y));
                    arr->set(2, value::Value(result->scale.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setScale(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setScale",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    return value::Value(std::monostate{});
                }

                services::TransformData newTransform = *currentTransform;
                newTransform.scale.x = extractFloat(args[1]);
                newTransform.scale.y = extractFloat(args[2]);
                newTransform.scale.z = extractFloat(args[3]);

                events::scene::SetTransformCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.transform = newTransform;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_entity_hasComponent(id, type) -> bool
        interpreter->registerNativeFunction("_native_entity_hasComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                std::string typeName = extractString(args[1]);
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    return value::Value(result->hasComponent(stringToComponentType(typeName)));
                }
                return value::Value(false);
            });

        // _native_entity_getComponents(id) -> string[]
        interpreter->registerNativeFunction("_native_entity_getComponents",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                    return value::Value(arr);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    auto arr = std::make_shared<value::NativeArray>(result->components.size(), value::ValueType::STRING);
                    for (size_t i = 0; i < result->components.size(); ++i) {
                        arr->set(i, value::Value(componentTypeToString(result->components[i])));
                    }
                    return value::Value(arr);
                }
                auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                return value::Value(arr);
            });

        // _native_entity_addComponent(id, type) -> bool
        interpreter->registerNativeFunction("_native_entity_addComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                std::string typeName = extractString(args[1]);
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);

                bool success = false;
                switch (compType) {
                    case services::ComponentTypeId::Camera: {
                        events::scene::AddCameraComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::Mesh: {
                        events::scene::AddMeshComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::Material: {
                        events::material::AddMaterialComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::AudioSource2D: {
                        events::scene::AddAudioSource2DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::AudioSource3D: {
                        events::scene::AddAudioSource3DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::Script: {
                        events::scripting::AttachScriptCommand cmd;
                        cmd.entity = entity;
                        cmd.data.scriptPath = "";  // Empty, user can attach script later
                        cmd.data.enabled = true;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    default:
                        spdlog::warn("[ScriptingAdapter] addComponent: unsupported component type '{}'", typeName);
                        break;
                }
                return value::Value(success);
            });

        // _native_entity_removeComponent(id, type) -> bool
        interpreter->registerNativeFunction("_native_entity_removeComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                std::string typeName = extractString(args[1]);
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);

                bool success = false;
                switch (compType) {
                    case services::ComponentTypeId::Camera: {
                        events::scene::RemoveCameraComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::Mesh: {
                        events::scene::RemoveMeshComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::Material: {
                        events::material::RemoveMaterialComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::AudioSource2D: {
                        events::scene::RemoveAudioSource2DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    case services::ComponentTypeId::AudioSource3D: {
                        events::scene::RemoveAudioSource3DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                    default:
                        spdlog::warn("[ScriptingAdapter] removeComponent: unsupported component type '{}'", typeName);
                        break;
                }
                return value::Value(success);
            });

        // _native_entity_setParent(id, parentId) -> bool
        // Set the parent of an entity. Use parentId = -1 to move to root
        interpreter->registerNativeFunction("_native_entity_setParent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                int64_t parentId = extractInt64(args[1]);
                if (id < 0) {
                    return value::Value(false);
                }

                events::scene::ReparentEntityCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.newParent = intToEntity(parentId);  // -1 will be invalid handle = root
                bool success = dispatcher.execute(cmd);
                return value::Value(success);
            });

        // _native_entity_getParent(id) -> int64 (parent ID, -1 if no parent)
        interpreter->registerNativeFunction("_native_entity_getParent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value() && result->parent.has_value()) {
                    return value::Value(entityToInt(result->parent.value()));
                }
                return value::Value(static_cast<int64_t>(-1));
            });

        // _native_entity_getChildren(id) -> int64[]
        interpreter->registerNativeFunction("_native_entity_getChildren",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    auto arr = std::make_shared<value::NativeArray>(result->children.size(), value::ValueType::INT);
                    for (size_t i = 0; i < result->children.size(); ++i) {
                        arr->set(i, value::Value(entityToInt(result->children[i])));
                    }
                    return value::Value(arr);
                }
                auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                return value::Value(arr);
            });

        // _native_entity_create(name, parentId?) -> int64 (new entity ID)
        // If parentId is provided and >= 0, creates as child of that entity
        // Otherwise creates at scene root
        interpreter->registerNativeFunction("_native_entity_create",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                std::string name = "New Entity";
                if (!args.empty()) {
                    name = extractString(args[0]);
                    if (name.empty()) {
                        name = "New Entity";
                    }
                }

                events::scene::CreateEntityCommand cmd;
                cmd.name = name;

                // Check for optional parent parameter
                if (args.size() >= 2) {
                    int64_t parentId = extractInt64(args[1]);
                    if (parentId >= 0) {
                        cmd.parent = intToEntity(parentId);
                    }
                }

                auto newHandle = dispatcher.execute(cmd);
                return value::Value(entityToInt(newHandle));
            });

        // _native_entity_destroy(id) -> void
        interpreter->registerNativeFunction("_native_entity_destroy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::DeleteEntityCommand cmd;
                cmd.entity = intToEntity(id);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        spdlog::debug("[ScriptingAdapter] Registered Entity native functions");
    }

    void ScriptingAdapter::registerAudioClass() {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_audio_play2d(entityId) -> int64 (audio handle, 0 if failed)
        interpreter->registerNativeFunction("_native_audio_play2d",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(0));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::AudioSource2DComponent>(entity)) {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                if (audioComp.audioFilePath.empty()) {
                    return value::Value(static_cast<int64_t>(0));
                }

                // Stop existing playback if any
                if (audioComp.activeHandle != 0) {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                    dispatcher.execute(stopCmd);
                }

                // Play streaming sound (2D audio uses streaming)
                events::audio::PlayStreamingSoundCommand cmd;
                cmd.path = audioComp.audioFilePath;
                cmd.params.volume = audioComp.volume;
                cmd.params.pitch = audioComp.pitch;
                cmd.params.loop = audioComp.loop;
                cmd.params.is3D = false;
                auto handle = dispatcher.execute(cmd);

                audioComp.activeHandle = handle.id;
                audioComp.isPlaying = true;

                return value::Value(static_cast<int64_t>(handle.id));
            });

        // _native_audio_play3d(entityId) -> int64 (audio handle, 0 if failed)
        interpreter->registerNativeFunction("_native_audio_play3d",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(0));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::AudioSource3DComponent>(entity)) {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                if (audioComp.audioFilePath.empty()) {
                    return value::Value(static_cast<int64_t>(0));
                }

                // Get position from transform
                glm::vec3 position(0.0f);
                if (registry.all_of<components::WorldTransformComponent>(entity)) {
                    auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                    position = glm::vec3(worldTransform.worldMatrix[3]);
                }

                // Stop existing playback if any
                if (audioComp.activeHandle != 0) {
                    events::audio::StopSoundCommand stopCmd;
                    stopCmd.handle = services::AudioHandle{audioComp.activeHandle};
                    dispatcher.execute(stopCmd);
                }

                // Play 3D sound
                events::audio::PlaySound3DCommand cmd;
                cmd.path = audioComp.audioFilePath;
                cmd.position = position;
                cmd.params.volume = audioComp.volume;
                cmd.params.pitch = audioComp.pitch;
                cmd.params.loop = audioComp.loop;
                cmd.params.is3D = true;
                cmd.params.minDistance = audioComp.minDistance;
                cmd.params.maxDistance = audioComp.maxDistance;
                auto handle = dispatcher.execute(cmd);

                audioComp.activeHandle = handle.id;
                audioComp.isPlaying = true;

                return value::Value(static_cast<int64_t>(handle.id));
            });

        // _native_audio_stop(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_stop",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::StopSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                        audioComp.activeHandle = 0;
                        audioComp.isPlaying = false;
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::StopSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                        audioComp.activeHandle = 0;
                        audioComp.isPlaying = false;
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_pause(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_pause",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::PauseSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::PauseSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_resume(entityId) -> void
        interpreter->registerNativeFunction("_native_audio_resume",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::ResumeSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::ResumeSoundCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_isPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_audio_isPlaying",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(false);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(false);
                }

                // Check for 2D audio component
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::IsSoundPlayingQuery query;
                        query.handle = services::AudioHandle{audioComp.activeHandle};
                        return value::Value(dispatcher.query(query));
                    }
                }

                // Check for 3D audio component
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    if (audioComp.activeHandle != 0) {
                        events::audio::IsSoundPlayingQuery query;
                        query.handle = services::AudioHandle{audioComp.activeHandle};
                        return value::Value(dispatcher.query(query));
                    }
                }

                return value::Value(false);
            });

        // _native_audio_setVolume(entityId, volume) -> void
        interpreter->registerNativeFunction("_native_audio_setVolume",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                float volume = 1.0f;
                if (std::holds_alternative<float>(args[1])) {
                    volume = std::get<float>(args[1]);
                } else if (std::holds_alternative<int64_t>(args[1])) {
                    volume = static_cast<float>(std::get<int64_t>(args[1]));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                // Update component and live audio for 2D
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    audioComp.volume = volume;
                    if (audioComp.activeHandle != 0) {
                        events::audio::SetSoundVolumeCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.volume = volume;
                        dispatcher.execute(cmd);
                    }
                }

                // Update component and live audio for 3D
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    audioComp.volume = volume;
                    if (audioComp.activeHandle != 0) {
                        events::audio::SetSoundVolumeCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.volume = volume;
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_setPitch(entityId, pitch) -> void
        interpreter->registerNativeFunction("_native_audio_setPitch",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                float pitch = 1.0f;
                if (std::holds_alternative<float>(args[1])) {
                    pitch = std::get<float>(args[1]);
                } else if (std::holds_alternative<int64_t>(args[1])) {
                    pitch = static_cast<float>(std::get<int64_t>(args[1]));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                // Update component and live audio for 2D
                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource2DComponent>(entity);
                    audioComp.pitch = pitch;
                    if (audioComp.activeHandle != 0) {
                        events::audio::SetSoundPitchCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.pitch = pitch;
                        dispatcher.execute(cmd);
                    }
                }

                // Update component and live audio for 3D
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    auto& audioComp = registry.get<components::AudioSource3DComponent>(entity);
                    audioComp.pitch = pitch;
                    if (audioComp.activeHandle != 0) {
                        events::audio::SetSoundPitchCommand cmd;
                        cmd.handle = services::AudioHandle{audioComp.activeHandle};
                        cmd.pitch = pitch;
                        dispatcher.execute(cmd);
                    }
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_getVolume(entityId) -> float
        interpreter->registerNativeFunction("_native_audio_getVolume",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(1.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(1.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(1.0f);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource2DComponent>(entity).volume);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource3DComponent>(entity).volume);
                }

                return value::Value(1.0f);
            });

        // _native_audio_getPitch(entityId) -> float
        interpreter->registerNativeFunction("_native_audio_getPitch",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(1.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(1.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(1.0f);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource2DComponent>(entity).pitch);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource3DComponent>(entity).pitch);
                }

                return value::Value(1.0f);
            });

        // _native_audio_setLoop(entityId, loop) -> void
        interpreter->registerNativeFunction("_native_audio_setLoop",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                bool loop = false;
                if (std::holds_alternative<bool>(args[1])) {
                    loop = std::get<bool>(args[1]);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(std::monostate{});
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    registry.get<components::AudioSource2DComponent>(entity).loop = loop;
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    registry.get<components::AudioSource3DComponent>(entity).loop = loop;
                }

                return value::Value(std::monostate{});
            });

        // _native_audio_getLoop(entityId) -> bool
        interpreter->registerNativeFunction("_native_audio_getLoop",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(false);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity)) {
                    return value::Value(false);
                }

                if (registry.all_of<components::AudioSource2DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource2DComponent>(entity).loop);
                }
                if (registry.all_of<components::AudioSource3DComponent>(entity)) {
                    return value::Value(registry.get<components::AudioSource3DComponent>(entity).loop);
                }

                return value::Value(false);
            });

        spdlog::debug("[ScriptingAdapter] Registered Audio native functions");
    }

    void ScriptingAdapter::registerInputClass() {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_input_isKeyDown(keyCode) -> bool
        interpreter->registerNativeFunction("_native_input_isKeyDown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyDownQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isMouseButtonDown(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonDown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonDownQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_getMouseX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseX",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value {
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.x);
            });

        // _native_input_getMouseY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseY",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value {
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.y);
            });

        // _native_input_getMouseDeltaX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaX",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value {
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.x);
            });

        // _native_input_getMouseDeltaY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaY",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value {
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.y);
            });

        // _native_input_isKeyReleased(keyCode) -> bool
        interpreter->registerNativeFunction("_native_input_isKeyReleased",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyReleasedQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isMouseButtonReleased(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonReleased",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonReleasedQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isDoubleClick(button) -> bool
        interpreter->registerNativeFunction("_native_input_isDoubleClick",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsDoubleClickQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });

        spdlog::debug("[ScriptingAdapter] Registered Input native functions");
    }

}
