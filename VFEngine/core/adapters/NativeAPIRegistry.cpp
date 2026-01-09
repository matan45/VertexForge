#include "NativeAPIRegistry.hpp"
#include <services/ScriptInterpreter.hpp>
#include <value/ValueType.hpp>
#include <value/NativeArray.hpp>
#include <runtimeTypes/klass/ObjectInstance.hpp>
#include <optional>

// Include event dispatcher for Entity API callbacks
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "../../services/events/MaterialEvents.hpp"
#include "../../services/events/AudioEvents.hpp"
#include "../../services/events/ScriptingEvents.hpp"
#include "../../services/events/InputEvents.hpp"
#include "../../services/events/PhysicsEvents.hpp"

// Include ECS for direct component access
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../services/data/EntityConversion.hpp"

#include "print/EditorLogger.hpp"

namespace core {

    NativeAPIRegistry::NativeAPIRegistry(::services::ScriptInterpreter* interp)
        : interpreter(interp)
    {
    }

    void NativeAPIRegistry::setCurrentEntity(const ::services::EntityHandle& entity) {
        currentCallbackEntity = entity;
    }

    ::services::EntityHandle NativeAPIRegistry::getCurrentEntity() {
        return currentCallbackEntity;
    }

    static std::string extractString(const value::Value& val, const char* context = nullptr) {
        if (std::holds_alternative<std::string>(val)) {
            return std::get<std::string>(val);
        }
        if (std::holds_alternative<value::InternedString>(val)) {
            return std::get<value::InternedString>(val).getString();
        }
        // Check for boxed String object
        if (std::holds_alternative<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val)) {
            auto obj = std::get<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val);
            if (obj) {
                if (obj->getTypeName() == "String") {
                    auto fieldVal = obj->getFieldValue("value");
                    if (std::holds_alternative<std::string>(fieldVal)) {
                        return std::get<std::string>(fieldVal);
                    }
                    if (std::holds_alternative<value::InternedString>(fieldVal)) {
                        return std::get<value::InternedString>(fieldVal).getString();
                    }
                }
                // Try to get _value field for other wrapper types
                auto valueField = obj->getFieldValue("_value");
                if (std::holds_alternative<std::string>(valueField)) {
                    return std::get<std::string>(valueField);
                }
                if (std::holds_alternative<value::InternedString>(valueField)) {
                    return std::get<value::InternedString>(valueField).getString();
                }
            }
        }
        if (context && !std::holds_alternative<std::monostate>(val)) {
            vfLogError("[Script] {}: expected string argument, got variant index {}", context, val.index());
        }
        return "";
    }

    static int64_t extractInt64(const value::Value& val, const char* context = nullptr) {
        if (std::holds_alternative<int64_t>(val)) {
            return std::get<int64_t>(val);
        }
        if (context && !std::holds_alternative<std::monostate>(val)) {
            vfLogError("[Script] {}: expected integer argument", context);
        }
        return -1;
    }

    static float extractFloat(const value::Value& val, const char* context = nullptr) {
        if (std::holds_alternative<float>(val)) {
            return std::get<float>(val);
        }
        if (std::holds_alternative<int64_t>(val)) {
            return static_cast<float>(std::get<int64_t>(val));
        }
        if (context && !std::holds_alternative<std::monostate>(val)) {
            vfLogError("[Script] {}: expected number argument", context);
        }
        return 0.0f;
    }

    static int64_t entityToInt(const services::EntityHandle& handle) {
        if (!handle.isValid()) {
            return -1;
        }
        return static_cast<int64_t>(handle.id);
    }

    static services::EntityHandle intToEntity(int64_t id) {
        if (id < 0) {
            return services::EntityHandle::invalid();
        }
        return services::EntityHandle{ static_cast<uint64_t>(id) };
    }

    static std::optional<services::ComponentTypeId> stringToComponentType(const std::string& type) {
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
        if (type == "RigidBody") return services::ComponentTypeId::RigidBody;
        if (type == "Collider") return services::ComponentTypeId::Collider;
        vfLogError("[Script] Unknown component type: '{}'", type);
        return std::nullopt;
    }

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
            case services::ComponentTypeId::RigidBody: return "RigidBody";
            case services::ComponentTypeId::Collider: return "Collider";
            default: return "Unknown";
        }
    }

    void NativeAPIRegistry::registerEngineAPIs() {
        registerLogClass();
        registerEntityClass();
        registerAudioClass();
        registerInputClass();
        registerPhysicsClass();

        vfLogInfo("[NativeAPIRegistry] Registered native engine APIs");
    }

    void NativeAPIRegistry::registerLogClass() {
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

    void NativeAPIRegistry::registerEntityClass() {
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
                    vfLogError("[Script] Entity.findByName: missing name argument");
                    return value::Value(static_cast<int64_t>(-1));
                }
                std::string name = extractString(args[0], "Entity.findByName");
                if (name.empty()) {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::scene::FindEntitiesByNameQuery query;
                query.name = name;
                auto results = dispatcher.query(query);

                if (!results.empty()) {
                    return value::Value(entityToInt(results[0]));
                }
                // Not found is expected behavior, don't log
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
                    vfLogError("[Script] Entity.findWithComponent: missing component type argument");
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                std::string typeName = extractString(args[0], "Entity.findWithComponent");
                if (typeName.empty()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::GetEntitiesWithComponentQuery query;
                query.componentType = *compType;
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
                    vfLogError("[Script] Entity.setPosition: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setPosition");
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                // Get current transform first
                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    vfLogError("[Script] Entity.setPosition: entity {} does not exist or has no transform", id);
                    return value::Value(std::monostate{});
                }

                // Update position
                services::TransformData newTransform = *currentTransform;
                newTransform.position.x = extractFloat(args[1], "Entity.setPosition");
                newTransform.position.y = extractFloat(args[2], "Entity.setPosition");
                newTransform.position.z = extractFloat(args[3], "Entity.setPosition");

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
                    vfLogError("[Script] Entity.setRotation: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setRotation");
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    vfLogError("[Script] Entity.setRotation: entity {} does not exist or has no transform", id);
                    return value::Value(std::monostate{});
                }

                services::TransformData newTransform = *currentTransform;
                newTransform.rotation.x = extractFloat(args[1], "Entity.setRotation");
                newTransform.rotation.y = extractFloat(args[2], "Entity.setRotation");
                newTransform.rotation.z = extractFloat(args[3], "Entity.setRotation");

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
                    vfLogError("[Script] Entity.setScale: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setScale");
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value()) {
                    vfLogError("[Script] Entity.setScale: entity {} does not exist or has no transform", id);
                    return value::Value(std::monostate{});
                }

                services::TransformData newTransform = *currentTransform;
                newTransform.scale.x = extractFloat(args[1], "Entity.setScale");
                newTransform.scale.y = extractFloat(args[2], "Entity.setScale");
                newTransform.scale.z = extractFloat(args[3], "Entity.setScale");

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
                    vfLogError("[Script] Entity.hasComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.hasComponent");
                std::string typeName = extractString(args[1], "Entity.hasComponent");
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) {
                    return value::Value(false);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value()) {
                    return value::Value(result->hasComponent(*compType));
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
                    vfLogError("[Script] Entity.addComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.addComponent");
                std::string typeName = extractString(args[1], "Entity.addComponent");
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) {
                    return value::Value(false);
                }

                bool success = false;
                switch (*compType) {
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
                        vfLogError("[Script] Entity.addComponent: component type '{}' cannot be added via script", typeName);
                        break;
                }
                return value::Value(success);
            });

        // _native_entity_removeComponent(id, type) -> bool
        interpreter->registerNativeFunction("_native_entity_removeComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    vfLogError("[Script] Entity.removeComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.removeComponent");
                std::string typeName = extractString(args[1], "Entity.removeComponent");
                if (id < 0 || typeName.empty()) {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) {
                    return value::Value(false);
                }

                bool success = false;
                switch (*compType) {
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
                        vfLogError("[Script] Entity.removeComponent: component type '{}' cannot be removed via script", typeName);
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

        vfLogInfo("[NativeAPIRegistry] Registered Entity native functions");
    }

    void NativeAPIRegistry::registerAudioClass() {
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

        vfLogInfo("[NativeAPIRegistry] Registered Audio native functions");
    }

    void NativeAPIRegistry::registerInputClass() {
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

        vfLogInfo("[NativeAPIRegistry] Registered Input native functions");
    }

    void NativeAPIRegistry::registerPhysicsClass() {
        auto& dispatcher = events::EventDispatcher::instance();

        // ============================================
        // RigidBody Queries
        // ============================================

        // _native_physics_hasRigidBody(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_hasRigidBody",
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

                return value::Value(registry.all_of<components::RigidBodyComponent>(entity));
            });

        // _native_physics_getBodyType(entityId) -> int (0=Static, 1=Dynamic, 2=Kinematic)
        interpreter->registerNativeFunction("_native_physics_getBodyType",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(1)); // Dynamic default
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(1));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(static_cast<int64_t>(1));
                }

                auto& rb = registry.get<components::RigidBodyComponent>(entity);
                return value::Value(static_cast<int64_t>(rb.type));
            });

        // _native_physics_getMass(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getMass",
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
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(1.0f);
                }

                return value::Value(registry.get<components::RigidBodyComponent>(entity).mass);
            });

        // _native_physics_getLinearDamping(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getLinearDamping",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(0.0f);
                }

                return value::Value(registry.get<components::RigidBodyComponent>(entity).linearDamping);
            });

        // _native_physics_getAngularDamping(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getAngularDamping",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(0.05f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(0.05f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(0.05f);
                }

                return value::Value(registry.get<components::RigidBodyComponent>(entity).angularDamping);
            });

        // _native_physics_getLinearVelocity(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getLinearVelocity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                result->set(1, value::Value(0.0f));
                result->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                events::physics::GetLinearVelocityQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                glm::vec3 vel = dispatcher.query(query);

                result->set(0, value::Value(vel.x));
                result->set(1, value::Value(vel.y));
                result->set(2, value::Value(vel.z));
                return value::Value(result);
            });

        // _native_physics_getAngularVelocity(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getAngularVelocity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                result->set(1, value::Value(0.0f));
                result->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                events::physics::GetAngularVelocityQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                glm::vec3 vel = dispatcher.query(query);

                result->set(0, value::Value(vel.x));
                result->set(1, value::Value(vel.y));
                result->set(2, value::Value(vel.z));
                return value::Value(result);
            });

        // ============================================
        // RigidBody Setters
        // ============================================

        // _native_physics_setBodyType(entityId, type) -> void
        interpreter->registerNativeFunction("_native_physics_setBodyType",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                int64_t type = extractInt64(args[1]);
                if (id < 0 || type < 0 || type > 2) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::RigidBodyComponent>(entity).type =
                    static_cast<components::RigidBodyType>(type);
                return value::Value(std::monostate{});
            });

        // _native_physics_setMass(entityId, mass) -> void
        interpreter->registerNativeFunction("_native_physics_setMass",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float mass = extractFloat(args[1]);
                if (id < 0 || mass < 0.0f) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::RigidBodyComponent>(entity).mass = mass;
                return value::Value(std::monostate{});
            });

        // _native_physics_setLinearDamping(entityId, damping) -> void
        interpreter->registerNativeFunction("_native_physics_setLinearDamping",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float damping = extractFloat(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::RigidBodyComponent>(entity).linearDamping = damping;
                return value::Value(std::monostate{});
            });

        // _native_physics_setAngularDamping(entityId, damping) -> void
        interpreter->registerNativeFunction("_native_physics_setAngularDamping",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float damping = extractFloat(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::RigidBodyComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::RigidBodyComponent>(entity).angularDamping = damping;
                return value::Value(std::monostate{});
            });

        // _native_physics_setLinearVelocity(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setLinearVelocity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::SetLinearVelocityCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.velocity = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_physics_setAngularVelocity(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setAngularVelocity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::SetAngularVelocityCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.velocity = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ============================================
        // Force and Impulse
        // ============================================

        // _native_physics_applyForce(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyForce",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::ApplyForceCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.force = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_physics_applyForceAtPosition(entityId, fx, fy, fz, px, py, pz) -> void
        interpreter->registerNativeFunction("_native_physics_applyForceAtPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 7) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float fx = extractFloat(args[1]);
                float fy = extractFloat(args[2]);
                float fz = extractFloat(args[3]);
                float px = extractFloat(args[4]);
                float py = extractFloat(args[5]);
                float pz = extractFloat(args[6]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::ApplyForceAtPositionCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.force = glm::vec3(fx, fy, fz);
                cmd.position = glm::vec3(px, py, pz);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_physics_applyImpulse(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyImpulse",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::ApplyImpulseCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.impulse = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_physics_applyTorque(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_applyTorque",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::ApplyTorqueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.torque = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ============================================
        // Physics Position/Rotation
        // ============================================

        // _native_physics_getPosition(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                result->set(1, value::Value(0.0f));
                result->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                events::physics::GetPhysicsPositionQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                glm::vec3 pos = dispatcher.query(query);

                result->set(0, value::Value(pos.x));
                result->set(1, value::Value(pos.y));
                result->set(2, value::Value(pos.z));
                return value::Value(result);
            });

        // _native_physics_setPosition(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::SetPhysicsPositionCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.position = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_physics_getRotation(entityId) -> float[] (x, y, z, w) quaternion
        interpreter->registerNativeFunction("_native_physics_getRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                result->set(1, value::Value(0.0f));
                result->set(2, value::Value(0.0f));
                result->set(3, value::Value(1.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                events::physics::GetPhysicsRotationQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                glm::quat rot = dispatcher.query(query);

                result->set(0, value::Value(rot.x));
                result->set(1, value::Value(rot.y));
                result->set(2, value::Value(rot.z));
                result->set(3, value::Value(rot.w));
                return value::Value(result);
            });

        // _native_physics_setRotation(entityId, x, y, z, w) -> void
        interpreter->registerNativeFunction("_native_physics_setRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 5) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                float w = extractFloat(args[4]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                events::physics::SetPhysicsRotationCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(id)};
                cmd.rotation = glm::quat(w, x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ============================================
        // Collider Queries
        // ============================================

        // _native_physics_hasCollider(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_hasCollider",
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

                return value::Value(registry.all_of<components::ColliderComponent>(entity));
            });

        // _native_physics_getColliderShape(entityId) -> int
        interpreter->registerNativeFunction("_native_physics_getColliderShape",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(0)); // Box default
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(0));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(static_cast<int64_t>(0));
                }

                return value::Value(static_cast<int64_t>(registry.get<components::ColliderComponent>(entity).shape));
            });

        // _native_physics_getColliderSize(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getColliderSize",
            [](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(1.0f));
                result->set(1, value::Value(1.0f));
                result->set(2, value::Value(1.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(result);
                }

                auto& col = registry.get<components::ColliderComponent>(entity);
                result->set(0, value::Value(col.size.x));
                result->set(1, value::Value(col.size.y));
                result->set(2, value::Value(col.size.z));
                return value::Value(result);
            });

        // _native_physics_getColliderHeight(entityId) -> float (capsule height)
        interpreter->registerNativeFunction("_native_physics_getColliderHeight",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(2.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(2.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(2.0f);
                }

                return value::Value(registry.get<components::ColliderComponent>(entity).height);
            });

        // _native_physics_getColliderOffset(entityId) -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getColliderOffset",
            [](const std::vector<value::Value>& args) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                result->set(0, value::Value(0.0f));
                result->set(1, value::Value(0.0f));
                result->set(2, value::Value(0.0f));

                if (args.empty()) {
                    return value::Value(result);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(result);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(result);
                }

                auto& col = registry.get<components::ColliderComponent>(entity);
                result->set(0, value::Value(col.offset.x));
                result->set(1, value::Value(col.offset.y));
                result->set(2, value::Value(col.offset.z));
                return value::Value(result);
            });

        // _native_physics_isTrigger(entityId) -> bool
        interpreter->registerNativeFunction("_native_physics_isTrigger",
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
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(false);
                }

                return value::Value(registry.get<components::ColliderComponent>(entity).isTrigger);
            });

        // _native_physics_getCollisionLayer(entityId) -> int
        interpreter->registerNativeFunction("_native_physics_getCollisionLayer",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(static_cast<int64_t>(1));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(static_cast<int64_t>(1));
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(static_cast<int64_t>(1));
                }

                return value::Value(static_cast<int64_t>(registry.get<components::ColliderComponent>(entity).collisionLayer));
            });

        // _native_physics_getFriction(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getFriction",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(0.5f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(0.5f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(0.5f);
                }

                return value::Value(registry.get<components::ColliderComponent>(entity).friction);
            });

        // _native_physics_getRestitution(entityId) -> float
        interpreter->registerNativeFunction("_native_physics_getRestitution",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.empty()) {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(0.0f);
                }

                return value::Value(registry.get<components::ColliderComponent>(entity).restitution);
            });

        // ============================================
        // Collider Setters
        // ============================================

        // _native_physics_setColliderSize(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setColliderSize",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 4) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float x = extractFloat(args[1]);
                float y = extractFloat(args[2]);
                float z = extractFloat(args[3]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).size = glm::vec3(x, y, z);
                return value::Value(std::monostate{});
            });

        // _native_physics_setColliderHeight(entityId, height) -> void
        interpreter->registerNativeFunction("_native_physics_setColliderHeight",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float height = extractFloat(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).height = height;
                return value::Value(std::monostate{});
            });

        // _native_physics_setTrigger(entityId, isTrigger) -> void
        interpreter->registerNativeFunction("_native_physics_setTrigger",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                bool isTrigger = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : false;
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).isTrigger = isTrigger;
                return value::Value(std::monostate{});
            });

        // _native_physics_setCollisionLayer(entityId, layer) -> void
        interpreter->registerNativeFunction("_native_physics_setCollisionLayer",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                int64_t layer = extractInt64(args[1]);
                if (id < 0 || layer < 0 || layer > 15) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).collisionLayer = static_cast<uint8_t>(layer);
                return value::Value(std::monostate{});
            });

        // _native_physics_setFriction(entityId, friction) -> void
        interpreter->registerNativeFunction("_native_physics_setFriction",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float friction = extractFloat(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).friction = friction;
                return value::Value(std::monostate{});
            });

        // _native_physics_setRestitution(entityId, restitution) -> void
        interpreter->registerNativeFunction("_native_physics_setRestitution",
            [](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                float restitution = extractFloat(args[1]);
                if (id < 0) {
                    return value::Value(std::monostate{});
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::ColliderComponent>(entity)) {
                    return value::Value(std::monostate{});
                }

                registry.get<components::ColliderComponent>(entity).restitution = restitution;
                return value::Value(std::monostate{});
            });

        // ============================================
        // Raycasting
        // ============================================

        // _native_physics_raycast(ox, oy, oz, dx, dy, dz, maxDist) -> float[]
        // Returns: [hit(0/1), entityId, px, py, pz, nx, ny, nz, distance]
        interpreter->registerNativeFunction("_native_physics_raycast",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 7) {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f)); // No hit
                    return value::Value(result);
                }

                float ox = extractFloat(args[0]);
                float oy = extractFloat(args[1]);
                float oz = extractFloat(args[2]);
                float dx = extractFloat(args[3]);
                float dy = extractFloat(args[4]);
                float dz = extractFloat(args[5]);
                float maxDist = extractFloat(args[6]);

                events::physics::RaycastQuery query;
                query.origin = glm::vec3(ox, oy, oz);
                query.direction = glm::vec3(dx, dy, dz);
                query.maxDistance = maxDist;
                services::RaycastHit hit = dispatcher.query(query);

                if (hit.hit) {
                    auto result = std::make_shared<value::NativeArray>(9, value::ValueType::FLOAT);
                    result->set(0, value::Value(1.0f)); // hit
                    result->set(1, value::Value(static_cast<float>(hit.entity.id)));
                    result->set(2, value::Value(hit.point.x));
                    result->set(3, value::Value(hit.point.y));
                    result->set(4, value::Value(hit.point.z));
                    result->set(5, value::Value(hit.normal.x));
                    result->set(6, value::Value(hit.normal.y));
                    result->set(7, value::Value(hit.normal.z));
                    result->set(8, value::Value(hit.distance));
                    return value::Value(result);
                } else {
                    auto result = std::make_shared<value::NativeArray>(1, value::ValueType::FLOAT);
                    result->set(0, value::Value(0.0f)); // No hit
                    return value::Value(result);
                }
            });

        // _native_physics_isOverlapping(entityA, entityB) -> bool
        interpreter->registerNativeFunction("_native_physics_isOverlapping",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 2) {
                    return value::Value(false);
                }
                int64_t idA = extractInt64(args[0]);
                int64_t idB = extractInt64(args[1]);
                if (idA < 0 || idB < 0) {
                    return value::Value(false);
                }

                events::physics::IsOverlappingQuery query;
                query.entityA = services::EntityHandle{static_cast<uint64_t>(idA)};
                query.entityB = services::EntityHandle{static_cast<uint64_t>(idB)};
                return value::Value(dispatcher.query(query));
            });

        // ============================================
        // Global Physics Settings
        // ============================================

        // _native_physics_getGravity() -> float[] (x, y, z)
        interpreter->registerNativeFunction("_native_physics_getGravity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value {
                auto result = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);

                events::physics::GetGravityQuery query;
                glm::vec3 gravity = dispatcher.query(query);

                result->set(0, value::Value(gravity.x));
                result->set(1, value::Value(gravity.y));
                result->set(2, value::Value(gravity.z));
                return value::Value(result);
            });

        // _native_physics_setGravity(x, y, z) -> void
        interpreter->registerNativeFunction("_native_physics_setGravity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value {
                if (args.size() < 3) {
                    return value::Value(std::monostate{});
                }
                float x = extractFloat(args[0]);
                float y = extractFloat(args[1]);
                float z = extractFloat(args[2]);

                events::physics::SetGravityCommand cmd;
                cmd.gravity = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        vfLogInfo("[NativeAPIRegistry] Registered Physics native functions");
    }

}
