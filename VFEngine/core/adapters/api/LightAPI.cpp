// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "LightAPI.hpp"
#include "NativeHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"

namespace core::api
{
    namespace
    {
        template<typename Component>
        std::optional<entt::entity> getValidEntity(const std::vector<value::Value>& args)
        {
            if (args.empty())
            {
                return std::nullopt;
            }
            int64_t id = extractInt64(args[0]);
            if (id < 0)
            {
                return std::nullopt;
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            auto entity = services::internal::fromHandle(services::EntityHandle{
                static_cast<uint64_t>(id)
            });
            if (!registry.valid(entity) || !registry.all_of<Component>(entity))
            {
                return std::nullopt;
            }
            return entity;
        }

        template<typename Component>
        value::Value getColor(const std::vector<value::Value>& args)
        {
            auto entityOpt = getValidEntity<Component>(args);
            if (!entityOpt)
            {
                return value::Value(std::monostate{});
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            const auto& comp = registry.get<Component>(*entityOpt);
            auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
            arr->set(0, value::Value(comp.color.r));
            arr->set(1, value::Value(comp.color.g));
            arr->set(2, value::Value(comp.color.b));
            return value::Value(arr);
        }

        template<typename Component>
        value::Value setColor(const std::vector<value::Value>& args, const char* context)
        {
            if (args.size() < 4)
            {
                return value::Value(std::monostate{});
            }

            auto entityOpt = getValidEntity<Component>(args);
            if (!entityOpt)
            {
                return value::Value(std::monostate{});
            }

            float r = extractFloat(args[1], context);
            float g = extractFloat(args[2], context);
            float b = extractFloat(args[3], context);

            auto& registry = scene::EntityRegistry::getRegistry();
            auto& comp = registry.get<Component>(*entityOpt);
            comp.color = glm::vec3(r, g, b);

            return value::Value(std::monostate{});
        }

        template<typename Component, auto MemberPtr>
        value::Value getFloatProperty(const std::vector<value::Value>& args)
        {
            auto entityOpt = getValidEntity<Component>(args);
            if (!entityOpt)
            {
                return value::Value(0.0f);
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            const auto& comp = registry.get<Component>(*entityOpt);
            return value::Value(comp.*MemberPtr);
        }

        template<typename Component, auto MemberPtr>
        value::Value setFloatProperty(const std::vector<value::Value>& args, const char* context)
        {
            if (args.size() < 2)
            {
                return value::Value(std::monostate{});
            }

            auto entityOpt = getValidEntity<Component>(args);
            if (!entityOpt)
            {
                return value::Value(std::monostate{});
            }

            float val = extractFloat(args[1], context);

            auto& registry = scene::EntityRegistry::getRegistry();
            auto& comp = registry.get<Component>(*entityOpt);
            comp.*MemberPtr = val;

            return value::Value(std::monostate{});
        }
    }

    void LightAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        using DirLight = components::DirectionalLightComponent;
        using PtLight = components::PointLightComponent;
        using SpLight = components::SpotLightComponent;

        interpreter->registerNativeFunction("_native_directionalLight_getColor",
            [](const std::vector<value::Value>& args) { return getColor<DirLight>(args); });

        interpreter->registerNativeFunction("_native_directionalLight_setColor",
            [](const std::vector<value::Value>& args) { return setColor<DirLight>(args, "DirectionalLight.setColor"); });

        interpreter->registerNativeFunction("_native_directionalLight_getIntensity",
            [](const std::vector<value::Value>& args) { return getFloatProperty<DirLight, &DirLight::intensity>(args); });

        interpreter->registerNativeFunction("_native_directionalLight_setIntensity",
            [](const std::vector<value::Value>& args) { return setFloatProperty<DirLight, &DirLight::intensity>(args, "DirectionalLight.setIntensity"); });

        interpreter->registerNativeFunction("_native_pointLight_getColor",
            [](const std::vector<value::Value>& args) { return getColor<PtLight>(args); });

        interpreter->registerNativeFunction("_native_pointLight_setColor",
            [](const std::vector<value::Value>& args) { return setColor<PtLight>(args, "PointLight.setColor"); });

        interpreter->registerNativeFunction("_native_pointLight_getIntensity",
            [](const std::vector<value::Value>& args) { return getFloatProperty<PtLight, &PtLight::intensity>(args); });

        interpreter->registerNativeFunction("_native_pointLight_setIntensity",
            [](const std::vector<value::Value>& args) { return setFloatProperty<PtLight, &PtLight::intensity>(args, "PointLight.setIntensity"); });

        interpreter->registerNativeFunction("_native_pointLight_getRadius",
            [](const std::vector<value::Value>& args) { return getFloatProperty<PtLight, &PtLight::radius>(args); });

        interpreter->registerNativeFunction("_native_pointLight_setRadius",
            [](const std::vector<value::Value>& args) { return setFloatProperty<PtLight, &PtLight::radius>(args, "PointLight.setRadius"); });

        interpreter->registerNativeFunction("_native_spotLight_getColor",
            [](const std::vector<value::Value>& args) { return getColor<SpLight>(args); });

        interpreter->registerNativeFunction("_native_spotLight_setColor",
            [](const std::vector<value::Value>& args) { return setColor<SpLight>(args, "SpotLight.setColor"); });

        interpreter->registerNativeFunction("_native_spotLight_getIntensity",
            [](const std::vector<value::Value>& args) { return getFloatProperty<SpLight, &SpLight::intensity>(args); });

        interpreter->registerNativeFunction("_native_spotLight_setIntensity",
            [](const std::vector<value::Value>& args) { return setFloatProperty<SpLight, &SpLight::intensity>(args, "SpotLight.setIntensity"); });

        interpreter->registerNativeFunction("_native_spotLight_getInnerAngle",
            [](const std::vector<value::Value>& args) { return getFloatProperty<SpLight, &SpLight::innerAngle>(args); });

        interpreter->registerNativeFunction("_native_spotLight_setInnerAngle",
            [](const std::vector<value::Value>& args) { return setFloatProperty<SpLight, &SpLight::innerAngle>(args, "SpotLight.setInnerAngle"); });

        interpreter->registerNativeFunction("_native_spotLight_getOuterAngle",
            [](const std::vector<value::Value>& args) { return getFloatProperty<SpLight, &SpLight::outerAngle>(args); });

        interpreter->registerNativeFunction("_native_spotLight_setOuterAngle",
            [](const std::vector<value::Value>& args) { return setFloatProperty<SpLight, &SpLight::outerAngle>(args, "SpotLight.setOuterAngle"); });

        interpreter->registerNativeFunction("_native_spotLight_getRange",
            [](const std::vector<value::Value>& args) { return getFloatProperty<SpLight, &SpLight::range>(args); });

        interpreter->registerNativeFunction("_native_spotLight_setRange",
            [](const std::vector<value::Value>& args) { return setFloatProperty<SpLight, &SpLight::range>(args, "SpotLight.setRange"); });
    }
}
