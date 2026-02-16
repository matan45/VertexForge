// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "EntityComponentAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/SceneEvents.hpp"
#include "../../../services/events/MaterialEvents.hpp"
#include "../../../services/events/ScriptingEvents.hpp"
#include "../../../services/events/UIEvents.hpp"

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
            };
            return map;
        }
    }

    void EntityComponentAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_entity_hasComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_entity_getComponents",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_entity_addComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_entity_removeComponent",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });
    }
}
