// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>

#include "MaterialAPI.hpp"
#include "NativeHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"

#include <span>

namespace core::api
{
    namespace
    {
        components::MaterialComponent* getMaterialComponent(std::span<const value::Value> args,
                                                            const char* context)
        {
            auto entityOpt = resolveEntity(args.empty() ? value::Value(std::monostate{}) : args[0]);
            if (!entityOpt)
            {
                return nullptr;
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            if (!registry.all_of<components::MaterialComponent>(*entityOpt))
            {
                vfLogWarning("[Script] {}: entity has no MaterialComponent", context);
                return nullptr;
            }
            return &registry.get<components::MaterialComponent>(*entityOpt);
        }

        value::Value setParameter(std::span<const value::Value> args,
                                  const char* context,
                                  size_t componentCount)
        {
            // args: entityId, name, components...
            if (args.size() < 2 + componentCount)
            {
                vfLogError("[Script] {}: expected {} arguments", context, 2 + componentCount);
                return value::Value(false);
            }

            auto* comp = getMaterialComponent(args, context);
            if (!comp)
            {
                return value::Value(false);
            }

            std::string name = extractString(args[1], context);
            if (name.empty())
            {
                return value::Value(false);
            }

            float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            for (size_t i = 0; i < componentCount; ++i)
            {
                c[i] = extractFloat(args[2 + i], context);
            }

            switch (componentCount)
            {
            case 1: comp->parameterOverrides[name] = c[0]; break;
            case 2: comp->parameterOverrides[name] = glm::vec2(c[0], c[1]); break;
            case 3: comp->parameterOverrides[name] = glm::vec3(c[0], c[1], c[2]); break;
            case 4: comp->parameterOverrides[name] = glm::vec4(c[0], c[1], c[2], c[3]); break;
            default: return value::Value(false);
            }
            return value::Value(true);
        }
    }

    void MaterialAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_material_setScalar",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                return setParameter(args, "Material.setScalar", 1);
            }});

        interpreter->registerNativeFunction("_native_material_setVec2",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                return setParameter(args, "Material.setVec2", 2);
            }});

        interpreter->registerNativeFunction("_native_material_setVec3",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                return setParameter(args, "Material.setVec3", 3);
            }});

        interpreter->registerNativeFunction("_native_material_setColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                return setParameter(args, "Material.setColor", 4);
            }});

        interpreter->registerNativeFunction("_native_material_getScalar",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto* comp = getMaterialComponent(args, "Material.getScalar");
                if (!comp || args.size() < 2)
                {
                    return value::Value(0.0f);
                }
                std::string name = extractString(args[1], "Material.getScalar");
                auto it = comp->parameterOverrides.find(name);
                if (it != comp->parameterOverrides.end())
                {
                    if (const float* v = std::get_if<float>(&it->second))
                    {
                        return value::Value(*v);
                    }
                }
                return value::Value(0.0f);
            }});

        interpreter->registerNativeFunction("_native_material_hasParameter",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto* comp = getMaterialComponent(args, "Material.hasParameter");
                if (!comp || args.size() < 2)
                {
                    return value::Value(false);
                }
                std::string name = extractString(args[1], "Material.hasParameter");
                return value::Value(comp->parameterOverrides.contains(name));
            }});

        interpreter->registerNativeFunction("_native_material_resetParameter",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto* comp = getMaterialComponent(args, "Material.resetParameter");
                if (!comp || args.size() < 2)
                {
                    return value::Value(false);
                }
                std::string name = extractString(args[1], "Material.resetParameter");
                return value::Value(comp->parameterOverrides.erase(name) > 0);
            }});

        interpreter->registerNativeFunction("_native_material_resetAllParameters",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto* comp = getMaterialComponent(args, "Material.resetAllParameters");
                if (!comp)
                {
                    return value::Value(false);
                }
                bool hadOverrides = !comp->parameterOverrides.empty();
                comp->parameterOverrides.clear();
                return value::Value(hadOverrides);
            }});
    }
}
