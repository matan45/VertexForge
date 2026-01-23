// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "LightAPI.hpp"
#include "NativeHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"

namespace core::api
{
    void LightAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // ============================================
        // Directional Light API
        // ============================================

        // _native_directionalLight_getColor(entityId) -> [r, g, b] as NativeArray
        interpreter->registerNativeFunction("_native_directionalLight_getColor",
            [](const std::vector<value::Value>& args) -> value::Value
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

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::DirectionalLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                const auto& comp = registry.get<components::DirectionalLightComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(comp.color.r));
                arr->set(1, value::Value(comp.color.g));
                arr->set(2, value::Value(comp.color.b));
                return value::Value(arr);
            });

        // _native_directionalLight_setColor(entityId, r, g, b) -> void
        interpreter->registerNativeFunction("_native_directionalLight_setColor",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float r = extractFloat(args[1], "DirectionalLight.setColor");
                float g = extractFloat(args[2], "DirectionalLight.setColor");
                float b = extractFloat(args[3], "DirectionalLight.setColor");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::DirectionalLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::DirectionalLightComponent>(entity);
                comp.color = glm::vec3(r, g, b);

                return value::Value(std::monostate{});
            });

        // _native_directionalLight_getIntensity(entityId) -> float
        interpreter->registerNativeFunction("_native_directionalLight_getIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::DirectionalLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::DirectionalLightComponent>(entity);
                return value::Value(comp.intensity);
            });

        // _native_directionalLight_setIntensity(entityId, intensity) -> void
        interpreter->registerNativeFunction("_native_directionalLight_setIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float intensity = extractFloat(args[1], "DirectionalLight.setIntensity");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::DirectionalLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::DirectionalLightComponent>(entity);
                comp.intensity = intensity;

                return value::Value(std::monostate{});
            });

        // ============================================
        // Point Light API
        // ============================================

        // _native_pointLight_getColor(entityId) -> [r, g, b] as NativeArray
        interpreter->registerNativeFunction("_native_pointLight_getColor",
            [](const std::vector<value::Value>& args) -> value::Value
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

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                const auto& comp = registry.get<components::PointLightComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(comp.color.r));
                arr->set(1, value::Value(comp.color.g));
                arr->set(2, value::Value(comp.color.b));
                return value::Value(arr);
            });

        // _native_pointLight_setColor(entityId, r, g, b) -> void
        interpreter->registerNativeFunction("_native_pointLight_setColor",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float r = extractFloat(args[1], "PointLight.setColor");
                float g = extractFloat(args[2], "PointLight.setColor");
                float b = extractFloat(args[3], "PointLight.setColor");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::PointLightComponent>(entity);
                comp.color = glm::vec3(r, g, b);

                return value::Value(std::monostate{});
            });

        // _native_pointLight_getIntensity(entityId) -> float
        interpreter->registerNativeFunction("_native_pointLight_getIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::PointLightComponent>(entity);
                return value::Value(comp.intensity);
            });

        // _native_pointLight_setIntensity(entityId, intensity) -> void
        interpreter->registerNativeFunction("_native_pointLight_setIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float intensity = extractFloat(args[1], "PointLight.setIntensity");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::PointLightComponent>(entity);
                comp.intensity = intensity;

                return value::Value(std::monostate{});
            });

        // _native_pointLight_getRadius(entityId) -> float
        interpreter->registerNativeFunction("_native_pointLight_getRadius",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::PointLightComponent>(entity);
                return value::Value(comp.radius);
            });

        // _native_pointLight_setRadius(entityId, radius) -> void
        interpreter->registerNativeFunction("_native_pointLight_setRadius",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float radius = extractFloat(args[1], "PointLight.setRadius");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::PointLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::PointLightComponent>(entity);
                comp.radius = radius;

                return value::Value(std::monostate{});
            });

        // ============================================
        // Spot Light API
        // ============================================

        // _native_spotLight_getColor(entityId) -> [r, g, b] as NativeArray
        interpreter->registerNativeFunction("_native_spotLight_getColor",
            [](const std::vector<value::Value>& args) -> value::Value
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

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                const auto& comp = registry.get<components::SpotLightComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(comp.color.r));
                arr->set(1, value::Value(comp.color.g));
                arr->set(2, value::Value(comp.color.b));
                return value::Value(arr);
            });

        // _native_spotLight_setColor(entityId, r, g, b) -> void
        interpreter->registerNativeFunction("_native_spotLight_setColor",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float r = extractFloat(args[1], "SpotLight.setColor");
                float g = extractFloat(args[2], "SpotLight.setColor");
                float b = extractFloat(args[3], "SpotLight.setColor");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::SpotLightComponent>(entity);
                comp.color = glm::vec3(r, g, b);

                return value::Value(std::monostate{});
            });

        // _native_spotLight_getIntensity(entityId) -> float
        interpreter->registerNativeFunction("_native_spotLight_getIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::SpotLightComponent>(entity);
                return value::Value(comp.intensity);
            });

        // _native_spotLight_setIntensity(entityId, intensity) -> void
        interpreter->registerNativeFunction("_native_spotLight_setIntensity",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float intensity = extractFloat(args[1], "SpotLight.setIntensity");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::SpotLightComponent>(entity);
                comp.intensity = intensity;

                return value::Value(std::monostate{});
            });

        // _native_spotLight_getInnerAngle(entityId) -> float (degrees)
        interpreter->registerNativeFunction("_native_spotLight_getInnerAngle",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::SpotLightComponent>(entity);
                return value::Value(comp.innerAngle);
            });

        // _native_spotLight_setInnerAngle(entityId, angle) -> void
        interpreter->registerNativeFunction("_native_spotLight_setInnerAngle",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float angle = extractFloat(args[1], "SpotLight.setInnerAngle");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::SpotLightComponent>(entity);
                comp.innerAngle = angle;

                return value::Value(std::monostate{});
            });

        // _native_spotLight_getOuterAngle(entityId) -> float (degrees)
        interpreter->registerNativeFunction("_native_spotLight_getOuterAngle",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::SpotLightComponent>(entity);
                return value::Value(comp.outerAngle);
            });

        // _native_spotLight_setOuterAngle(entityId, angle) -> void
        interpreter->registerNativeFunction("_native_spotLight_setOuterAngle",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float angle = extractFloat(args[1], "SpotLight.setOuterAngle");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::SpotLightComponent>(entity);
                comp.outerAngle = angle;

                return value::Value(std::monostate{});
            });

        // _native_spotLight_getRange(entityId) -> float
        interpreter->registerNativeFunction("_native_spotLight_getRange",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(0.0f);
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(0.0f);
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(0.0f);
                }

                const auto& comp = registry.get<components::SpotLightComponent>(entity);
                return value::Value(comp.range);
            });

        // _native_spotLight_setRange(entityId, range) -> void
        interpreter->registerNativeFunction("_native_spotLight_setRange",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0)
                {
                    return value::Value(std::monostate{});
                }

                float range = extractFloat(args[1], "SpotLight.setRange");

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{
                    static_cast<uint64_t>(id)
                });
                if (!registry.valid(entity) || !registry.all_of<
                    components::SpotLightComponent>(entity))
                {
                    return value::Value(std::monostate{});
                }

                auto& comp = registry.get<components::SpotLightComponent>(entity);
                comp.range = range;

                return value::Value(std::monostate{});
            });
    }
}
