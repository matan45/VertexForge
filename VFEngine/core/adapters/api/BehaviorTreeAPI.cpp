// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "BehaviorTreeAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ai/BehaviorTreeEvents.hpp"

namespace core::api
{
    void BehaviorTreeAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // === Blackboard setters ===

        interpreter->registerNativeFunction("_native_bt_setBlackboardFloat",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                std::string key = extractString(args[1]);
                float val = extractFloat(args[2]);

                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.key = key;
                cmd.value = val;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_bt_setBlackboardInt",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                std::string key = extractString(args[1]);
                int32_t val = static_cast<int32_t>(extractInt64(args[2]));

                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.key = key;
                cmd.value = val;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_bt_setBlackboardBool",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                std::string key = extractString(args[1]);
                bool val = extractBool(args[2]);

                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.key = key;
                cmd.value = val;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_bt_setBlackboardString",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                std::string key = extractString(args[1]);
                std::string val = extractString(args[2]);

                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.key = key;
                cmd.value = val;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_bt_setBlackboardVec3",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 5) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                std::string key = extractString(args[1]);
                float x = extractFloat(args[2]);
                float y = extractFloat(args[3]);
                float z = extractFloat(args[4]);

                events::ai::SetBlackboardValueCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.key = key;
                cmd.value = glm::vec3(x, y, z);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // === Blackboard getters ===

        interpreter->registerNativeFunction("_native_bt_getBlackboardFloat",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(0.0);
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(0.0);

                events::ai::GetBlackboardValueQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                auto result = dispatcher.query(query);
                if (std::holds_alternative<float>(result))
                    return value::Value(static_cast<double>(std::get<float>(result)));
                return value::Value(0.0);
            });

        interpreter->registerNativeFunction("_native_bt_getBlackboardInt",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(static_cast<int64_t>(0));
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(static_cast<int64_t>(0));

                events::ai::GetBlackboardValueQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                auto result = dispatcher.query(query);
                if (std::holds_alternative<int32_t>(result))
                    return value::Value(static_cast<int64_t>(std::get<int32_t>(result)));
                return value::Value(static_cast<int64_t>(0));
            });

        interpreter->registerNativeFunction("_native_bt_getBlackboardBool",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(false);
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(false);

                events::ai::GetBlackboardValueQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                auto result = dispatcher.query(query);
                if (std::holds_alternative<bool>(result))
                    return value::Value(std::get<bool>(result));
                return value::Value(false);
            });

        interpreter->registerNativeFunction("_native_bt_getBlackboardString",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::string{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::string{});

                events::ai::GetBlackboardValueQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                auto result = dispatcher.query(query);
                if (std::holds_alternative<std::string>(result))
                    return value::Value(std::get<std::string>(result));
                return value::Value(std::string{});
            });

        interpreter->registerNativeFunction("_native_bt_getBlackboardVec3",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return makeVec3Array(glm::vec3(0.0f));
                auto entity = resolveEntity(args[0]);
                if (!entity) return makeVec3Array(glm::vec3(0.0f));

                events::ai::GetBlackboardValueQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                auto result = dispatcher.query(query);
                if (std::holds_alternative<glm::vec3>(result))
                    return makeVec3Array(std::get<glm::vec3>(result));
                return makeVec3Array(glm::vec3(0.0f));
            });

        // === Blackboard key check ===

        interpreter->registerNativeFunction("_native_bt_hasBlackboardKey",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(false);
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(false);

                events::ai::HasBlackboardKeyQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                query.key = extractString(args[1]);
                return value::Value(dispatcher.query(query));
            });

        // === Behavior Tree control ===

        interpreter->registerNativeFunction("_native_bt_hasBehaviorTree",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(false);

                events::ai::HasBehaviorTreeQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_bt_setBehaviorTreeEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::monostate{});

                events::ai::SetBehaviorTreeEnabledCommand cmd;
                cmd.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                cmd.enabled = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_bt_isEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(false);

                events::ai::IsBehaviorTreeEnabledQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                return value::Value(dispatcher.query(query));
            });

        interpreter->registerNativeFunction("_native_bt_getStatus",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::string{"stopped"});
                auto entity = resolveEntity(args[0]);
                if (!entity) return value::Value(std::string{"stopped"});

                events::ai::GetBehaviorTreeStatusQuery query;
                query.entity = services::EntityHandle{static_cast<uint64_t>(extractInt64(args[0]))};
                return value::Value(dispatcher.query(query));
            });
    }
}
