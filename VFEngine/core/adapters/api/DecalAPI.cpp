// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "DecalAPI.hpp"
#include "NativeHelpers.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/EntityConversion.hpp"
#include <algorithm>

namespace core::api
{
    void DecalAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_decal_getColor(entityId) -> float[] {r, g, b, a}
        interpreter->registerNativeFunction("_native_decal_getColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                const auto& comp = registry.get<components::DecalComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(4, value::ValueType::FLOAT);
                arr->set(0, value::Value(static_cast<double>(comp.color.r)));
                arr->set(1, value::Value(static_cast<double>(comp.color.g)));
                arr->set(2, value::Value(static_cast<double>(comp.color.b)));
                arr->set(3, value::Value(static_cast<double>(comp.color.a)));
                return value::Value(arr);
            }});

        // _native_decal_setColor(entityId, r, g, b, a) -> void
        interpreter->registerNativeFunction("_native_decal_setColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 5) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                auto& comp = registry.get<components::DecalComponent>(entity);
                comp.color.r = extractFloat(args[1]);
                comp.color.g = extractFloat(args[2]);
                comp.color.b = extractFloat(args[3]);
                comp.color.a = extractFloat(args[4]);
                return value::Value(std::monostate{});
            }});

        // _native_decal_getHalfExtents(entityId) -> float[] {x, y, z}
        interpreter->registerNativeFunction("_native_decal_getHalfExtents",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                const auto& comp = registry.get<components::DecalComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
                arr->set(0, value::Value(static_cast<double>(comp.halfExtents.x)));
                arr->set(1, value::Value(static_cast<double>(comp.halfExtents.y)));
                arr->set(2, value::Value(static_cast<double>(comp.halfExtents.z)));
                return value::Value(arr);
            }});

        // _native_decal_setHalfExtents(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_decal_setHalfExtents",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 4) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                auto& comp = registry.get<components::DecalComponent>(entity);
                comp.halfExtents.x = extractFloat(args[1]);
                comp.halfExtents.y = extractFloat(args[2]);
                comp.halfExtents.z = extractFloat(args[3]);
                return value::Value(std::monostate{});
            }});

        // _native_decal_getAngleFade(entityId) -> float[] {start, end}
        interpreter->registerNativeFunction("_native_decal_getAngleFade",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                const auto& comp = registry.get<components::DecalComponent>(entity);
                auto arr = std::make_shared<value::NativeArray>(2, value::ValueType::FLOAT);
                arr->set(0, value::Value(static_cast<double>(comp.angleFadeStart)));
                arr->set(1, value::Value(static_cast<double>(comp.angleFadeEnd)));
                return value::Value(arr);
            }});

        // _native_decal_setAngleFade(entityId, start, end) -> void
        interpreter->registerNativeFunction("_native_decal_setAngleFade",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 3) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                auto& comp = registry.get<components::DecalComponent>(entity);
                comp.angleFadeStart = extractFloat(args[1]);
                comp.angleFadeEnd = extractFloat(args[2]);
                return value::Value(std::monostate{});
            }});

        // _native_decal_getEdgeFalloff(entityId) -> float
        interpreter->registerNativeFunction("_native_decal_getEdgeFalloff",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(0.0);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(0.0);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(0.0);

                return value::Value(static_cast<double>(registry.get<components::DecalComponent>(entity).edgeFalloff));
            }});

        // _native_decal_setEdgeFalloff(entityId, falloff) -> void
        interpreter->registerNativeFunction("_native_decal_setEdgeFalloff",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                registry.get<components::DecalComponent>(entity).edgeFalloff =
                    extractFloat(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_decal_getSortPriority(entityId) -> int
        interpreter->registerNativeFunction("_native_decal_getSortPriority",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(static_cast<int64_t>(0));

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(static_cast<int64_t>(0));

                return value::Value(static_cast<int64_t>(registry.get<components::DecalComponent>(entity).sortPriority));
            }});

        // _native_decal_setSortPriority(entityId, priority) -> void
        interpreter->registerNativeFunction("_native_decal_setSortPriority",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                registry.get<components::DecalComponent>(entity).sortPriority =
                    static_cast<int32_t>(extractInt64(args[1]));
                return value::Value(std::monostate{});
            }});

        // _native_decal_getNormalStrength(entityId) -> float
        interpreter->registerNativeFunction("_native_decal_getNormalStrength",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(0.0);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(0.0);

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(0.0);

                return value::Value(static_cast<double>(registry.get<components::DecalComponent>(entity).normalStrength));
            }});

        // _native_decal_setNormalStrength(entityId, strength) -> void
        interpreter->registerNativeFunction("_native_decal_setNormalStrength",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                registry.get<components::DecalComponent>(entity).normalStrength =
                    extractFloat(args[1]);
                return value::Value(std::monostate{});
            }});

        // _native_decal_getShape(entityId) -> int (0=Rectangle, 1=Circle, 2=Triangle)
        interpreter->registerNativeFunction("_native_decal_getShape",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(static_cast<int64_t>(0));

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(static_cast<int64_t>(0));

                return value::Value(static_cast<int64_t>(registry.get<components::DecalComponent>(entity).shape));
            }});

        // _native_decal_setShape(entityId, shape) -> void
        interpreter->registerNativeFunction("_native_decal_setShape",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.size() < 2) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = services::internal::fromHandle(services::EntityHandle{static_cast<uint64_t>(id)});
                if (!registry.valid(entity) || !registry.all_of<components::DecalComponent>(entity))
                    return value::Value(std::monostate{});

                int64_t shape = std::clamp<int64_t>(extractInt64(args[1]), 0, 2);
                registry.get<components::DecalComponent>(entity).shape =
                    static_cast<components::DecalShape>(shape);
                return value::Value(std::monostate{});
            }});
    }
}
