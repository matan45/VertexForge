// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "EntityAPI.hpp"
#include "NativeHelpers.hpp"
#include "../NativeAPIRegistry.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "../../../services/events/MaterialEvents.hpp"
#include "../../../services/events/ScriptingEvents.hpp"
#include "../../../services/events/UIEvents.hpp"

namespace core::api
{
    void EntityAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_entity_getSelf() -> int64 (current script's entity)
        interpreter->registerNativeFunction("_native_entity_getSelf",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                return value::Value(entityToInt(NativeAPIRegistry::getCurrentEntity()));
            });

        // _native_entity_findByName(name) -> int64 (first match, -1 if not found)
        interpreter->registerNativeFunction("_native_entity_findByName",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Entity.findByName: missing name argument");
                    return value::Value(static_cast<int64_t>(-1));
                }
                std::string name = extractString(args[0], "Entity.findByName");
                if (name.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::scene::FindEntitiesByNameQuery query;
                query.name = name;
                auto results = dispatcher.query(query);

                if (!results.empty())
                {
                    return value::Value(entityToInt(results[0]));
                }
                return value::Value(static_cast<int64_t>(-1));
            });

        // _native_entity_findAll(name) -> int64[] (all matches)
        interpreter->registerNativeFunction("_native_entity_findAll",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                std::string name = extractString(args[0]);
                if (name.empty())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::FindEntitiesByNameQuery query;
                query.name = name;
                auto results = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(results.size(), value::ValueType::INT);
                for (size_t i = 0; i < results.size(); ++i)
                {
                    arr->set(i, value::Value(entityToInt(results[i])));
                }
                return value::Value(arr);
            });

        // _native_entity_findWithComponent(type) -> int64[] (entities with component)
        interpreter->registerNativeFunction("_native_entity_findWithComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Entity.findWithComponent: missing component type argument");
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                std::string typeName = extractString(args[0], "Entity.findWithComponent");
                if (typeName.empty())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::GetEntitiesWithComponentQuery query;
                query.componentType = *compType;
                auto results = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(results.size(), value::ValueType::INT);
                for (size_t i = 0; i < results.size(); ++i)
                {
                    arr->set(i, value::Value(entityToInt(results[i])));
                }
                return value::Value(arr);
            });

        // _native_entity_isValid(id) -> bool
        interpreter->registerNativeFunction("_native_entity_isValid",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(false);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    return value::Value(result->isActive);
                }
                return value::Value(false);
            });

        // _native_entity_setActive(id, active) -> void
        interpreter->registerNativeFunction("_native_entity_setActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                bool active = std::get<bool>(args[1]);
                if (id < 0)
                {
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::string(""));
                }

                services::EntityHandle handle = intToEntity(id);
                events::scene::GetEntityQuery query;
                query.entity = handle;
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    return value::Value(result->name);
                }
                return value::Value(std::string(""));
            });

        // _native_entity_setName(id, name) -> void
        interpreter->registerNativeFunction("_native_entity_setName",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                std::string name = extractString(args[1]);
                if (id < 0)
                {
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(0.0f));
                arr->set(1, value::Value(0.0f));
                arr->set(2, value::Value(0.0f));

                if (args.empty())
                {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    arr->set(0, value::Value(result->position.x));
                    arr->set(1, value::Value(result->position.y));
                    arr->set(2, value::Value(result->position.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setPosition(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setPosition",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    vfLogError("[Script] Entity.setPosition: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setPosition");
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value())
                {
                    vfLogError("[Script] Entity.setPosition: entity {} does not exist or has no transform", id);
                    return value::Value(std::monostate{});
                }

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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(0.0f));
                arr->set(1, value::Value(0.0f));
                arr->set(2, value::Value(0.0f));

                if (args.empty())
                {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    arr->set(0, value::Value(result->rotation.x));
                    arr->set(1, value::Value(result->rotation.y));
                    arr->set(2, value::Value(result->rotation.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setRotation(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setRotation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    vfLogError("[Script] Entity.setRotation: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setRotation");
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value())
                {
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(1.0f));
                arr->set(1, value::Value(1.0f));
                arr->set(2, value::Value(1.0f));

                if (args.empty())
                {
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(arr);
                }

                events::scene::GetTransformQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    arr->set(0, value::Value(result->scale.x));
                    arr->set(1, value::Value(result->scale.y));
                    arr->set(2, value::Value(result->scale.z));
                }
                return value::Value(arr);
            });

        // _native_entity_setScale(id, x, y, z) -> void
        interpreter->registerNativeFunction("_native_entity_setScale",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    vfLogError("[Script] Entity.setScale: missing arguments (expected entity id, x, y, z)");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0], "Entity.setScale");
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                events::scene::GetTransformQuery getQuery;
                getQuery.entity = intToEntity(id);
                auto currentTransform = dispatcher.query(getQuery);
                if (!currentTransform.has_value())
                {
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.hasComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.hasComponent");
                std::string typeName = extractString(args[1], "Entity.hasComponent");
                if (id < 0 || typeName.empty())
                {
                    return value::Value(false);
                }

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value())
                {
                    return value::Value(false);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    return value::Value(result->hasComponent(*compType));
                }
                return value::Value(false);
            });

        // _native_entity_getComponents(id) -> string[]
        interpreter->registerNativeFunction("_native_entity_getComponents",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                    return value::Value(arr);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    auto arr = std::make_shared<value::NativeArray>(result->components.size(), value::ValueType::STRING);
                    for (size_t i = 0; i < result->components.size(); ++i)
                    {
                        arr->set(i, value::Value(componentTypeToString(result->components[i])));
                    }
                    return value::Value(arr);
                }
                auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::STRING);
                return value::Value(arr);
            });

        // _native_entity_addComponent(id, type) -> bool
        interpreter->registerNativeFunction("_native_entity_addComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.addComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.addComponent");
                std::string typeName = extractString(args[1], "Entity.addComponent");
                if (id < 0 || typeName.empty())
                {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);
                if (!compType.has_value())
                {
                    return value::Value(false);
                }

                bool success = false;
                switch (*compType)
                {
                case services::ComponentTypeId::Camera:
                    {
                        events::scene::AddCameraComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::Mesh:
                    {
                        events::scene::AddMeshComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::Material:
                    {
                        events::material::AddMaterialComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::AudioSource2D:
                    {
                        events::scene::AddAudioSource2DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::AudioSource3D:
                    {
                        events::scene::AddAudioSource3DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::Script:
                    {
                        events::scripting::AttachScriptCommand cmd;
                        cmd.entity = entity;
                        cmd.data.scriptPath = "";
                        cmd.data.enabled = true;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UICanvas:
                    {
                        events::ui::AddUICanvasComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIRect:
                    {
                        events::ui::AddUIRectComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIImage:
                    {
                        events::ui::AddUIImageComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIScroll:
                    {
                        events::ui::AddUIScrollComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UILayoutGroup:
                    {
                        events::ui::AddUILayoutGroupComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UILabel:
                    {
                        events::ui::AddUILabelComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIButton:
                    {
                        events::ui::AddUIButtonComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UITextInput:
                    {
                        events::ui::AddUITextInputComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UICheckbox:
                    {
                        events::ui::AddUICheckboxComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIDropdown:
                    {
                        events::ui::AddUIDropdownComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UITabs:
                    {
                        events::ui::AddUITabsComponentCommand cmd;
                        cmd.entity = entity;
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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.removeComponent: missing arguments (expected entity id and component type)");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.removeComponent");
                std::string typeName = extractString(args[1], "Entity.removeComponent");
                if (id < 0 || typeName.empty())
                {
                    return value::Value(false);
                }

                auto entity = intToEntity(id);
                auto compType = stringToComponentType(typeName);
                if (!compType.has_value())
                {
                    return value::Value(false);
                }

                bool success = false;
                switch (*compType)
                {
                case services::ComponentTypeId::Camera:
                    {
                        events::scene::RemoveCameraComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::Mesh:
                    {
                        events::scene::RemoveMeshComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::Material:
                    {
                        events::material::RemoveMaterialComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::AudioSource2D:
                    {
                        events::scene::RemoveAudioSource2DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::AudioSource3D:
                    {
                        events::scene::RemoveAudioSource3DComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UICanvas:
                    {
                        events::ui::RemoveUICanvasComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIRect:
                    {
                        events::ui::RemoveUIRectComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIImage:
                    {
                        events::ui::RemoveUIImageComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIScroll:
                    {
                        events::ui::RemoveUIScrollComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UILayoutGroup:
                    {
                        events::ui::RemoveUILayoutGroupComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UILabel:
                    {
                        events::ui::RemoveUILabelComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIButton:
                    {
                        events::ui::RemoveUIButtonComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UITextInput:
                    {
                        events::ui::RemoveUITextInputComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UICheckbox:
                    {
                        events::ui::RemoveUICheckboxComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UIDropdown:
                    {
                        events::ui::RemoveUIDropdownComponentCommand cmd;
                        cmd.entity = entity;
                        success = dispatcher.execute(cmd);
                        break;
                    }
                case services::ComponentTypeId::UITabs:
                    {
                        events::ui::RemoveUITabsComponentCommand cmd;
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
        interpreter->registerNativeFunction("_native_entity_setParent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0]);
                int64_t parentId = extractInt64(args[1]);
                if (id < 0)
                {
                    return value::Value(false);
                }

                events::scene::ReparentEntityCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.newParent = intToEntity(parentId);
                bool success = dispatcher.execute(cmd);
                return value::Value(success);
            });

        // _native_entity_getParent(id) -> int64 (parent ID, -1 if no parent)
        interpreter->registerNativeFunction("_native_entity_getParent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value() && result->parent.has_value())
                {
                    return value::Value(entityToInt(result->parent.value()));
                }
                return value::Value(static_cast<int64_t>(-1));
            });

        // _native_entity_getChildren(id) -> int64[]
        interpreter->registerNativeFunction("_native_entity_getChildren",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                    return value::Value(arr);
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    auto arr = std::make_shared<value::NativeArray>(result->children.size(), value::ValueType::INT);
                    for (size_t i = 0; i < result->children.size(); ++i)
                    {
                        arr->set(i, value::Value(entityToInt(result->children[i])));
                    }
                    return value::Value(arr);
                }
                auto arr = std::make_shared<value::NativeArray>(0, value::ValueType::INT);
                return value::Value(arr);
            });

        // _native_entity_create(name, parentId?) -> int64 (new entity ID)
        interpreter->registerNativeFunction("_native_entity_create",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                std::string name = "New Entity";
                if (!args.empty())
                {
                    name = extractString(args[0]);
                    if (name.empty())
                    {
                        name = "New Entity";
                    }
                }

                events::scene::CreateEntityCommand cmd;
                cmd.name = name;

                if (args.size() >= 2)
                {
                    int64_t parentId = extractInt64(args[1]);
                    if (parentId >= 0)
                    {
                        cmd.parent = intToEntity(parentId);
                    }
                }

                auto newHandle = dispatcher.execute(cmd);
                return value::Value(entityToInt(newHandle));
            });

        // _native_entity_destroy(id) -> void
        interpreter->registerNativeFunction("_native_entity_destroy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                events::scene::DeleteEntityCommand cmd;
                cmd.entity = intToEntity(id);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });
    }
}
