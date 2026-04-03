// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "DestructionAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/destruction/DestructionEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core::api
{
    void DestructionAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Destruction.applyDamage(entityId, amount, [damageType], [impactX,Y,Z], [dirX,Y,Z])
        interpreter->registerNativeFunction("_native_destruction_applyDamage",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                auto entity = intToEntity(extractInt64(args[0]));
                float amount = extractFloat(args[1]);

                events::destruction::ApplyDamageCommand cmd;
                cmd.entity = entity;
                cmd.amount = amount;

                if (args.size() >= 3)
                    cmd.damageType = static_cast<components::DamageType>(extractInt64(args[2]));
                if (args.size() >= 6)
                    cmd.impactPoint = glm::vec3(extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5]));
                if (args.size() >= 9)
                    cmd.impactDirection = glm::vec3(extractFloat(args[6]), extractFloat(args[7]), extractFloat(args[8]));

                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // Destruction.destroy(entityId)
        interpreter->registerNativeFunction("_native_destruction_destroy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});

                events::destruction::TriggerDestructionCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.force = 10.0f;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // Destruction.explode(cx, cy, cz, radius, damage, force)
        interpreter->registerNativeFunction("_native_destruction_explode",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 6) return value::Value(std::monostate{});

                events::destruction::ExplosionDamageCommand cmd;
                cmd.center = glm::vec3(extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2]));
                cmd.radius = extractFloat(args[3]);
                cmd.damage = extractFloat(args[4]);
                cmd.force = extractFloat(args[5]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // Destruction.getHealth(entityId) -> float
        interpreter->registerNativeFunction("_native_destruction_getHealth",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(0.0f);

                events::destruction::GetHealthQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                float health = dispatcher.query(query);
                return value::Value(health);
            });

        // Destruction.setHealth(entityId, hp)
        interpreter->registerNativeFunction("_native_destruction_setHealth",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto* comp = registry.try_get<components::DestructibleComponent>(*entity);
                if (comp)
                {
                    comp->currentHealth = glm::clamp(extractFloat(args[1]), 0.0f, comp->maxHealth);
                }
                return value::Value(std::monostate{});
            });

        // Destruction.isDestroyed(entityId) -> bool
        interpreter->registerNativeFunction("_native_destruction_isDestroyed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);

                events::destruction::IsDestroyedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                bool destroyed = dispatcher.query(query);
                return value::Value(destroyed);
            });

        // Destruction.repair(entityId)
        interpreter->registerNativeFunction("_native_destruction_repair",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});

                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                auto& registry = scene::EntityRegistry::getRegistry();
                auto* comp = registry.try_get<components::DestructibleComponent>(*entity);
                if (comp)
                {
                    comp->currentHealth = comp->maxHealth;
                    comp->isDestroyed = false;
                }
                return value::Value(std::monostate{});
            });
    }
}
