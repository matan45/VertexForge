// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "EntityComponentAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/project/SceneEvents.hpp"
#include "../../../services/events/scene/ComponentPhysicsLightEvents.hpp"
#include "../../../services/events/render/MaterialEvents.hpp"
#include "../../../services/events/scripting/ScriptingEvents.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/events/physics/SocketEvents.hpp"
#include "../../../services/events/scene/ComponentMediaEvents.hpp"

namespace core::api
{
    namespace
    {
        using AddCommandFn = std::function<bool(events::EventDispatcher&, services::EntityHandle)>;
        using RemoveCommandFn = std::function<bool(events::EventDispatcher&, services::EntityHandle)>;

        template<typename Command>
        AddCommandFn makeAddFn()
        {
            return [](events::EventDispatcher& d, services::EntityHandle e) -> bool
            {
                Command cmd;
                cmd.entity = e;
                return d.execute(cmd);
            };
        }

        template<typename Command>
        RemoveCommandFn makeRemoveFn()
        {
            return [](events::EventDispatcher& d, services::EntityHandle e) -> bool
            {
                Command cmd;
                cmd.entity = e;
                return d.execute(cmd);
            };
        }

        AddCommandFn makeAddScriptFn()
        {
            return [](events::EventDispatcher& d, services::EntityHandle e) -> bool
            {
                events::scripting::AttachScriptCommand cmd;
                cmd.entity = e;
                cmd.data.scriptPath = "";
                cmd.data.enabled = true;
                return d.execute(cmd);
            };
        }

        const std::unordered_map<services::ComponentTypeId, AddCommandFn>& getAddCommandMap()
        {
            static const std::unordered_map<services::ComponentTypeId, AddCommandFn> map = {
                {services::ComponentTypeId::Camera, makeAddFn<events::scene::AddCameraComponentCommand>()},
                {services::ComponentTypeId::Mesh, makeAddFn<events::scene::AddMeshComponentCommand>()},
                {services::ComponentTypeId::Material, makeAddFn<events::material::AddMaterialComponentCommand>()},
                {services::ComponentTypeId::AudioSource2D, makeAddFn<events::scene::AddAudioSource2DComponentCommand>()},
                {services::ComponentTypeId::AudioSource3D, makeAddFn<events::scene::AddAudioSource3DComponentCommand>()},
                {services::ComponentTypeId::Collider, makeAddFn<events::scene::AddColliderComponentCommand>()},
                {services::ComponentTypeId::RigidBody, makeAddFn<events::scene::AddRigidBodyComponentCommand>()},
                {services::ComponentTypeId::Vehicle, makeAddFn<events::scene::AddVehicleComponentCommand>()},
                {services::ComponentTypeId::PhysicsAnimation, makeAddFn<events::scene::AddPhysicsAnimationComponentCommand>()},
                {services::ComponentTypeId::Script, makeAddScriptFn()},
                {services::ComponentTypeId::UICanvas, makeAddFn<events::ui::AddUICanvasComponentCommand>()},
                {services::ComponentTypeId::UIRect, makeAddFn<events::ui::AddUIRectComponentCommand>()},
                {services::ComponentTypeId::UIImage, makeAddFn<events::ui::AddUIImageComponentCommand>()},
                {services::ComponentTypeId::UIScroll, makeAddFn<events::ui::AddUIScrollComponentCommand>()},
                {services::ComponentTypeId::UILayoutGroup, makeAddFn<events::ui::AddUILayoutGroupComponentCommand>()},
                {services::ComponentTypeId::UILabel, makeAddFn<events::ui::AddUILabelComponentCommand>()},
                {services::ComponentTypeId::UIButton, makeAddFn<events::ui::AddUIButtonComponentCommand>()},
                {services::ComponentTypeId::UITextInput, makeAddFn<events::ui::AddUITextInputComponentCommand>()},
                {services::ComponentTypeId::UICheckbox, makeAddFn<events::ui::AddUICheckboxComponentCommand>()},
                {services::ComponentTypeId::UIDropdown, makeAddFn<events::ui::AddUIDropdownComponentCommand>()},
                {services::ComponentTypeId::UITabs, makeAddFn<events::ui::AddUITabsComponentCommand>()},
                {services::ComponentTypeId::UISlider, makeAddFn<events::ui::AddUISliderComponentCommand>()},
                {services::ComponentTypeId::UIProgressBar, makeAddFn<events::ui::AddUIProgressBarComponentCommand>()},
                {services::ComponentTypeId::UIStyle, makeAddFn<events::ui::AddUIStyleComponentCommand>()},
                {services::ComponentTypeId::UITooltip, makeAddFn<events::ui::AddUITooltipComponentCommand>()},
                {services::ComponentTypeId::UIWindow, makeAddFn<events::ui::AddUIWindowComponentCommand>()},
                {services::ComponentTypeId::UIListView, makeAddFn<events::ui::AddUIListViewComponentCommand>()},
                {services::ComponentTypeId::SocketAttachment, makeAddFn<events::socket::AddSocketAttachmentComponentCommand>()},
                {services::ComponentTypeId::SocketOverride, makeAddFn<events::socket::AddSocketOverrideComponentCommand>()},
                {services::ComponentTypeId::NavmeshAgent, makeAddFn<events::scene::AddNavmeshAgentComponentCommand>()},
                {services::ComponentTypeId::Controller, makeAddFn<events::scene::AddControllerComponentCommand>()},
                {services::ComponentTypeId::Decal, makeAddFn<events::scene::AddDecalComponentCommand>()},
            };
            return map;
        }

        const std::unordered_map<services::ComponentTypeId, RemoveCommandFn>& getRemoveCommandMap()
        {
            static const std::unordered_map<services::ComponentTypeId, RemoveCommandFn> map = {
                {services::ComponentTypeId::Camera, makeRemoveFn<events::scene::RemoveCameraComponentCommand>()},
                {services::ComponentTypeId::Mesh, makeRemoveFn<events::scene::RemoveMeshComponentCommand>()},
                {services::ComponentTypeId::Material, makeRemoveFn<events::material::RemoveMaterialComponentCommand>()},
                {services::ComponentTypeId::AudioSource2D, makeRemoveFn<events::scene::RemoveAudioSource2DComponentCommand>()},
                {services::ComponentTypeId::AudioSource3D, makeRemoveFn<events::scene::RemoveAudioSource3DComponentCommand>()},
                {services::ComponentTypeId::Collider, makeRemoveFn<events::scene::RemoveColliderComponentCommand>()},
                {services::ComponentTypeId::RigidBody, makeRemoveFn<events::scene::RemoveRigidBodyComponentCommand>()},
                {services::ComponentTypeId::Vehicle, makeRemoveFn<events::scene::RemoveVehicleComponentCommand>()},
                {services::ComponentTypeId::PhysicsAnimation, makeRemoveFn<events::scene::RemovePhysicsAnimationComponentCommand>()},
                {services::ComponentTypeId::UICanvas, makeRemoveFn<events::ui::RemoveUICanvasComponentCommand>()},
                {services::ComponentTypeId::UIRect, makeRemoveFn<events::ui::RemoveUIRectComponentCommand>()},
                {services::ComponentTypeId::UIImage, makeRemoveFn<events::ui::RemoveUIImageComponentCommand>()},
                {services::ComponentTypeId::UIScroll, makeRemoveFn<events::ui::RemoveUIScrollComponentCommand>()},
                {services::ComponentTypeId::UILayoutGroup, makeRemoveFn<events::ui::RemoveUILayoutGroupComponentCommand>()},
                {services::ComponentTypeId::UILabel, makeRemoveFn<events::ui::RemoveUILabelComponentCommand>()},
                {services::ComponentTypeId::UIButton, makeRemoveFn<events::ui::RemoveUIButtonComponentCommand>()},
                {services::ComponentTypeId::UITextInput, makeRemoveFn<events::ui::RemoveUITextInputComponentCommand>()},
                {services::ComponentTypeId::UICheckbox, makeRemoveFn<events::ui::RemoveUICheckboxComponentCommand>()},
                {services::ComponentTypeId::UIDropdown, makeRemoveFn<events::ui::RemoveUIDropdownComponentCommand>()},
                {services::ComponentTypeId::UITabs, makeRemoveFn<events::ui::RemoveUITabsComponentCommand>()},
                {services::ComponentTypeId::UISlider, makeRemoveFn<events::ui::RemoveUISliderComponentCommand>()},
                {services::ComponentTypeId::UIProgressBar, makeRemoveFn<events::ui::RemoveUIProgressBarComponentCommand>()},
                {services::ComponentTypeId::UIStyle, makeRemoveFn<events::ui::RemoveUIStyleComponentCommand>()},
                {services::ComponentTypeId::UITooltip, makeRemoveFn<events::ui::RemoveUITooltipComponentCommand>()},
                {services::ComponentTypeId::UIWindow, makeRemoveFn<events::ui::RemoveUIWindowComponentCommand>()},
                {services::ComponentTypeId::UIListView, makeRemoveFn<events::ui::RemoveUIListViewComponentCommand>()},
                {services::ComponentTypeId::SocketAttachment, makeRemoveFn<events::socket::RemoveSocketAttachmentComponentCommand>()},
                {services::ComponentTypeId::SocketOverride, makeRemoveFn<events::socket::RemoveSocketOverrideComponentCommand>()},
                {services::ComponentTypeId::NavmeshAgent, makeRemoveFn<events::scene::RemoveNavmeshAgentComponentCommand>()},
                {services::ComponentTypeId::Controller, makeRemoveFn<events::scene::RemoveControllerComponentCommand>()},
                {services::ComponentTypeId::Decal, makeRemoveFn<events::scene::RemoveDecalComponentCommand>()},
            };
            return map;
        }
    }

    void EntityComponentAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_entity_hasComponent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.hasComponent: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.hasComponent");
                std::string typeName = extractString(args[1], "Entity.hasComponent");
                if (id < 0 || typeName.empty()) return value::Value(false);

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) return value::Value(false);

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    return value::Value(result->hasComponent(*compType));
                }
                return value::Value(false);
            }});

        interpreter->registerNativeFunction("_native_entity_getComponents",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(std::make_shared<value::NativeArray>(
                        0, value::ValueType::STRING));
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::make_shared<value::NativeArray>(
                        0, value::ValueType::STRING));
                }

                events::scene::GetEntityQuery query;
                query.entity = intToEntity(id);
                auto result = dispatcher.query(query);
                if (result.has_value())
                {
                    auto arr = std::make_shared<value::NativeArray>(
                        result->components.size(), value::ValueType::STRING);
                    for (size_t i = 0; i < result->components.size(); ++i)
                    {
                        arr->set(i, value::Value(componentTypeToString(result->components[i])));
                    }
                    return value::Value(arr);
                }
                return value::Value(std::make_shared<value::NativeArray>(
                    0, value::ValueType::STRING));
            }});

        interpreter->registerNativeFunction("_native_entity_addComponent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.addComponent: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.addComponent");
                std::string typeName = extractString(args[1], "Entity.addComponent");
                if (id < 0 || typeName.empty()) return value::Value(false);

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) return value::Value(false);

                auto& addMap = getAddCommandMap();
                auto it = addMap.find(*compType);
                if (it == addMap.end())
                {
                    vfLogError("[Script] Entity.addComponent: type '{}' cannot be added via script",
                               typeName);
                    return value::Value(false);
                }
                return value::Value(it->second(dispatcher, intToEntity(id)));
            }});

        interpreter->registerNativeFunction("_native_entity_removeComponent",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Entity.removeComponent: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Entity.removeComponent");
                std::string typeName = extractString(args[1], "Entity.removeComponent");
                if (id < 0 || typeName.empty()) return value::Value(false);

                auto compType = stringToComponentType(typeName);
                if (!compType.has_value()) return value::Value(false);

                auto& removeMap = getRemoveCommandMap();
                auto it = removeMap.find(*compType);
                if (it == removeMap.end())
                {
                    vfLogError("[Script] Entity.removeComponent: type '{}' cannot be removed via script",
                               typeName);
                    return value::Value(false);
                }
                return value::Value(it->second(dispatcher, intToEntity(id)));
            }});

        // _native_mesh_setMesh(entityId, meshPath) -> bool
        // Points an entity's MeshComponent at a .vfMesh asset by path (adding the
        // component first if absent). Routes through SetMeshDataCommand so the mesh
        // service handles asset lifecycle + streaming (GPU upload happens at render).
        interpreter->registerNativeFunction("_native_mesh_setMesh",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Mesh.setMesh: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Mesh.setMesh");
                std::string path = extractString(args[1], "Mesh.setMesh");
                if (id < 0 || path.empty()) return value::Value(false);

                auto entity = intToEntity(id);

                events::scene::AddMeshComponentCommand addCmd;
                addCmd.entity = entity;
                dispatcher.execute(addCmd);

                events::scene::SetMeshDataCommand setCmd;
                setCmd.entity = entity;
                setCmd.meshData.meshRef = asset::AssetRef::fromPath(path);
                return value::Value(dispatcher.execute(setCmd));
            }});

        // _native_material_setMaterial(entityId, materialPath) -> bool
        // Points an entity's MaterialComponent default material at a .vfMaterial
        // asset by path. SetDefaultMaterialCommand adds the component if absent and
        // takes the path directly (handles AssetRef::fromPath + lifecycle).
        interpreter->registerNativeFunction("_native_material_setMaterial",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Material.setMaterial: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Material.setMaterial");
                std::string path = extractString(args[1], "Material.setMaterial");
                if (id < 0 || path.empty()) return value::Value(false);

                events::material::SetDefaultMaterialCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.materialPath = path;
                return value::Value(dispatcher.execute(cmd));
            }});

        // _native_mesh_setRenderLayer(entityId, layer) -> bool  (VK-1415)
        // Sets the entity's render-layer index (clamped 0-31), preserving the rest of
        // its MeshData. A camera renders this mesh only if its cullingMask has the
        // matching bit set. Returns false if the entity has no MeshComponent.
        interpreter->registerNativeFunction("_native_mesh_setRenderLayer",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2)
                {
                    vfLogError("[Script] Mesh.setRenderLayer: missing arguments");
                    return value::Value(false);
                }
                int64_t id = extractInt64(args[0], "Mesh.setRenderLayer");
                int64_t layer = extractInt64(args[1], "Mesh.setRenderLayer");
                if (id < 0) return value::Value(false);
                if (layer < 0) layer = 0;
                if (layer > 31) layer = 31;

                auto entity = intToEntity(id);
                events::scene::GetMeshDataQuery q;
                q.entity = entity;
                auto current = dispatcher.query(q);
                if (!current.has_value()) return value::Value(false);

                current->renderLayer = static_cast<uint32_t>(layer);

                events::scene::SetMeshDataCommand setCmd;
                setCmd.entity = entity;
                setCmd.meshData = *current;
                return value::Value(dispatcher.execute(setCmd));
            }});

        // _native_mesh_getRenderLayer(entityId) -> int  (VK-1415); -1 if no mesh.
        interpreter->registerNativeFunction("_native_mesh_getRenderLayer",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                int64_t id = extractInt64(args[0], "Mesh.getRenderLayer");
                if (id < 0) return value::Value(static_cast<int64_t>(-1));
                events::scene::GetMeshDataQuery q;
                q.entity = intToEntity(id);
                auto result = dispatcher.query(q);
                if (!result.has_value()) return value::Value(static_cast<int64_t>(-1));
                return value::Value(static_cast<int64_t>(result->renderLayer));
            }});
    }
}
