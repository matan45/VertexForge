// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "AnimatorAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/AnimatorEvents.hpp"

namespace core::api
{
    void AnimatorAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // ============================================================
        // PARAMETER SETTERS
        // ============================================================

        // _native_animator_setFloat(entityId, paramName, value) -> void
        interpreter->registerNativeFunction("_native_animator_setFloat",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] Animator.setFloat: missing arguments (expected entityId, paramName, value)");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.setFloat");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                std::string paramName = extractString(args[1], "Animator.setFloat");
                float value = extractFloat(args[2], "Animator.setFloat");

                services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.parameterName = paramName;
                cmd.value = value;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_setInt(entityId, paramName, value) -> void
        interpreter->registerNativeFunction("_native_animator_setInt",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] Animator.setInt: missing arguments (expected entityId, paramName, value)");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.setInt");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                std::string paramName = extractString(args[1], "Animator.setInt");
                int32_t value = static_cast<int32_t>(extractInt64(args[2], "Animator.setInt"));

                services::events::animator::SetEntityAnimatorIntCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.parameterName = paramName;
                cmd.value = value;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_setBool(entityId, paramName, value) -> void
        interpreter->registerNativeFunction("_native_animator_setBool",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3)
                {
                    vfLogError("[Script] Animator.setBool: missing arguments (expected entityId, paramName, value)");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.setBool");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                std::string paramName = extractString(args[1], "Animator.setBool");
                bool value = std::get<bool>(args[2]);

                services::events::animator::SetEntityAnimatorBoolCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.parameterName = paramName;
                cmd.value = value;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_setTrigger(entityId, paramName) -> void
        interpreter->registerNativeFunction("_native_animator_setTrigger",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Animator.setTrigger: missing arguments (expected entityId, paramName)");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.setTrigger");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                std::string paramName = extractString(args[1], "Animator.setTrigger");

                services::events::animator::SetEntityAnimatorTriggerCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.parameterName = paramName;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ============================================================
        // PARAMETER GETTERS
        // ============================================================

        // _native_animator_getFloat(entityId, paramName) -> float
        interpreter->registerNativeFunction("_native_animator_getFloat",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Animator.getFloat: missing arguments (expected entityId, paramName)");
                    return value::Value(0.0f);
                }

                int64_t entityId = extractInt64(args[0], "Animator.getFloat");
                if (entityId < 0)
                {
                    return value::Value(0.0f);
                }

                std::string paramName = extractString(args[1], "Animator.getFloat");

                services::events::animator::GetEntityAnimatorFloatQuery query;
                query.entity = intToEntity(entityId);
                query.parameterName = paramName;

                return value::Value(dispatcher.query(query));
            });

        // _native_animator_getInt(entityId, paramName) -> int
        interpreter->registerNativeFunction("_native_animator_getInt",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Animator.getInt: missing arguments (expected entityId, paramName)");
                    return value::Value(static_cast<int64_t>(0));
                }

                int64_t entityId = extractInt64(args[0], "Animator.getInt");
                if (entityId < 0)
                {
                    return value::Value(static_cast<int64_t>(0));
                }

                std::string paramName = extractString(args[1], "Animator.getInt");

                services::events::animator::GetEntityAnimatorIntQuery query;
                query.entity = intToEntity(entityId);
                query.parameterName = paramName;

                return value::Value(static_cast<int64_t>(dispatcher.query(query)));
            });

        // _native_animator_getBool(entityId, paramName) -> bool
        interpreter->registerNativeFunction("_native_animator_getBool",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Animator.getBool: missing arguments (expected entityId, paramName)");
                    return value::Value(false);
                }

                int64_t entityId = extractInt64(args[0], "Animator.getBool");
                if (entityId < 0)
                {
                    return value::Value(false);
                }

                std::string paramName = extractString(args[1], "Animator.getBool");

                services::events::animator::GetEntityAnimatorBoolQuery query;
                query.entity = intToEntity(entityId);
                query.parameterName = paramName;

                return value::Value(dispatcher.query(query));
            });

        // ============================================================
        // PLAYBACK CONTROL
        // ============================================================

        // _native_animator_play(entityId) -> void
        interpreter->registerNativeFunction("_native_animator_play",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.play: missing entityId argument");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.play");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::animator::PlayEntityAnimatorCommand cmd;
                cmd.entity = intToEntity(entityId);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_pause(entityId) -> void
        interpreter->registerNativeFunction("_native_animator_pause",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.pause: missing entityId argument");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.pause");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::animator::PauseEntityAnimatorCommand cmd;
                cmd.entity = intToEntity(entityId);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_stop(entityId) -> void
        interpreter->registerNativeFunction("_native_animator_stop",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.stop: missing entityId argument");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.stop");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::animator::StopEntityAnimatorCommand cmd;
                cmd.entity = intToEntity(entityId);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_animator_reset(entityId) -> void
        interpreter->registerNativeFunction("_native_animator_reset",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.reset: missing entityId argument");
                    return value::Value(std::monostate{});
                }

                int64_t entityId = extractInt64(args[0], "Animator.reset");
                if (entityId < 0)
                {
                    return value::Value(std::monostate{});
                }

                services::events::animator::ResetEntityAnimatorCommand cmd;
                cmd.entity = intToEntity(entityId);
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // ============================================================
        // STATE QUERIES
        // ============================================================

        // _native_animator_isPlaying(entityId) -> bool
        interpreter->registerNativeFunction("_native_animator_isPlaying",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.isPlaying: missing entityId argument");
                    return value::Value(false);
                }

                int64_t entityId = extractInt64(args[0], "Animator.isPlaying");
                if (entityId < 0)
                {
                    return value::Value(false);
                }

                services::events::animator::IsEntityAnimatorPlayingQuery query;
                query.entity = intToEntity(entityId);

                return value::Value(dispatcher.query(query));
            });

        // _native_animator_isBlending(entityId) -> bool
        interpreter->registerNativeFunction("_native_animator_isBlending",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.isBlending: missing entityId argument");
                    return value::Value(false);
                }

                int64_t entityId = extractInt64(args[0], "Animator.isBlending");
                if (entityId < 0)
                {
                    return value::Value(false);
                }

                services::events::animator::IsEntityAnimatorBlendingQuery query;
                query.entity = intToEntity(entityId);

                return value::Value(dispatcher.query(query));
            });

        // _native_animator_getCurrentState(entityId) -> string
        interpreter->registerNativeFunction("_native_animator_getCurrentState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.getCurrentState: missing entityId argument");
                    return value::Value(std::string(""));
                }

                int64_t entityId = extractInt64(args[0], "Animator.getCurrentState");
                if (entityId < 0)
                {
                    return value::Value(std::string(""));
                }

                services::events::animator::GetEntityAnimatorCurrentStateQuery query;
                query.entity = intToEntity(entityId);

                return value::Value(dispatcher.query(query));
            });

        // _native_animator_getNormalizedTime(entityId) -> float
        interpreter->registerNativeFunction("_native_animator_getNormalizedTime",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.getNormalizedTime: missing entityId argument");
                    return value::Value(0.0f);
                }

                int64_t entityId = extractInt64(args[0], "Animator.getNormalizedTime");
                if (entityId < 0)
                {
                    return value::Value(0.0f);
                }

                services::events::animator::GetEntityAnimatorNormalizedTimeQuery query;
                query.entity = intToEntity(entityId);

                return value::Value(dispatcher.query(query));
            });

        // _native_animator_hasAnimator(entityId) -> bool
        interpreter->registerNativeFunction("_native_animator_hasAnimator",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Animator.hasAnimator: missing entityId argument");
                    return value::Value(false);
                }

                int64_t entityId = extractInt64(args[0], "Animator.hasAnimator");
                if (entityId < 0)
                {
                    return value::Value(false);
                }

                services::events::animator::HasEntityAnimatorQuery query;
                query.entity = intToEntity(entityId);

                return value::Value(dispatcher.query(query));
            });

        // ============================================================
        // TRANSITION CONTROL
        // ============================================================

        // _native_animator_forceTransitionTo(entityId, stateName, blendDuration?) -> bool
        interpreter->registerNativeFunction("_native_animator_forceTransitionTo",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Animator.forceTransitionTo: missing arguments (expected entityId, stateName, [blendDuration])");
                    return value::Value(false);
                }

                int64_t entityId = extractInt64(args[0], "Animator.forceTransitionTo");
                if (entityId < 0)
                {
                    return value::Value(false);
                }

                std::string stateName = extractString(args[1], "Animator.forceTransitionTo");
                float blendDuration = 0.25f;
                if (args.size() >= 3)
                {
                    blendDuration = extractFloat(args[2], "Animator.forceTransitionTo");
                }

                services::events::animator::ForceEntityTransitionToCommand cmd;
                cmd.entity = intToEntity(entityId);
                cmd.stateName = stateName;
                cmd.blendDuration = blendDuration;

                return value::Value(dispatcher.execute(cmd));
            });
    }
}
