// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PhysicsAnimationAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/physics/PhysicsAnimationEvents.hpp"

namespace core::api
{
    namespace
    {
        glm::vec3 clampMagnitude(glm::vec3 v, float maxMag)
        {
            float mag = glm::length(v);
            if (mag > maxMag)
            {
                return (v / mag) * maxMag;
            }
            return v;
        }
    }

    void PhysicsAnimationAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // ============================================
        // Queries
        // ============================================

        interpreter->registerNativeFunction("_native_physanim_hasPhysicsAnimation",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(false);

                events::physicsAnimation::HasPhysicsAnimationQuery query;
                query.entity = intToEntity(id);
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_physanim_isRagdollActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(false);

                events::physicsAnimation::IsRagdollActiveQuery query;
                query.entity = intToEntity(id);
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_physanim_getMode",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(static_cast<int64_t>(0));

                events::physicsAnimation::GetPhysicsAnimationModeQuery query;
                query.entity = intToEntity(id);
                auto mode = dispatcher.query(query);
                return value::Value(static_cast<int64_t>(mode));
            });

        // ============================================
        // Ragdoll Control
        // ============================================

        interpreter->registerNativeFunction("_native_physanim_activateRagdoll",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] PhysicsAnimation.activateRagdoll: missing entity ID");
                    return value::Value(std::monostate{});
                }
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                events::physicsAnimation::ActivateRagdollCommand cmd;
                cmd.entity = intToEntity(id);

                // Optional impulse (args[1], args[2], args[3])
                if (args.size() >= 4)
                {
                    cmd.impulse = clampMagnitude(
                        glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                        MAX_RAGDOLL_IMPULSE_MAGNITUDE);
                }

                // Optional bone index (args[4])
                if (args.size() >= 5)
                {
                    cmd.impulseAnimBoneIndex = static_cast<int>(extractInt64(args[4]));
                }

                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_physanim_deactivateRagdoll",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                events::physicsAnimation::DeactivateRagdollCommand cmd;
                cmd.entity = intToEntity(id);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // ============================================
        // Impulse Application
        // ============================================

        interpreter->registerNativeFunction("_native_physanim_applyRagdollImpulse",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                events::physicsAnimation::ApplyRagdollImpulseCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.impulse = clampMagnitude(
                    glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3])),
                    MAX_RAGDOLL_IMPULSE_MAGNITUDE);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_physanim_applyRagdollBoneImpulse",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 5) return value::Value(std::monostate{});
                int64_t id = extractInt64(args[0]);
                if (id < 0) return value::Value(std::monostate{});

                events::physicsAnimation::ApplyRagdollBoneImpulseCommand cmd;
                cmd.entity = intToEntity(id);
                cmd.animBoneIndex = static_cast<int>(extractInt64(args[1]));
                cmd.impulse = clampMagnitude(
                    glm::vec3(extractFloat(args[2]), extractFloat(args[3]), extractFloat(args[4])),
                    MAX_RAGDOLL_IMPULSE_MAGNITUDE);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        vfLogInfo("[PhysicsAnimationAPI] Registered PhysicsAnimation native functions");
    }
}
