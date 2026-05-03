// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "GameConfigAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/save/ConfigEvents.hpp"

namespace core::api
{
    void GameConfigAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // === Int ===
        interpreter->registerNativeFunction("_native_config_setInt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                events::save::SetConfigIntCommand cmd;
                cmd.key = extractString(args[0], "Config.setInt");
                cmd.value = extractInt64(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_config_getInt",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(static_cast<int64_t>(0));
                events::save::GetConfigIntQuery query;
                query.key = extractString(args[0], "Config.getInt");
                query.defaultValue = extractInt64(args[1]);
                return value::Value(dispatcher.query(query));
            }});

        // === Float ===
        interpreter->registerNativeFunction("_native_config_setFloat",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                events::save::SetConfigFloatCommand cmd;
                cmd.key = extractString(args[0], "Config.setFloat");
                cmd.value = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_config_getFloat",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(0.0);
                events::save::GetConfigFloatQuery query;
                query.key = extractString(args[0], "Config.getFloat");
                query.defaultValue = extractFloat(args[1]);
                return value::Value(dispatcher.query(query));
            }});

        // === String ===
        interpreter->registerNativeFunction("_native_config_setString",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                events::save::SetConfigStringCommand cmd;
                cmd.key = extractString(args[0], "Config.setString");
                cmd.value = extractString(args[1], "Config.setString.value");
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_config_getString",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::string(""));
                events::save::GetConfigStringQuery query;
                query.key = extractString(args[0], "Config.getString");
                query.defaultValue = extractString(args[1], "Config.getString.default");
                return value::Value(dispatcher.query(query));
            }});

        // === Bool ===
        interpreter->registerNativeFunction("_native_config_setBool",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                events::save::SetConfigBoolCommand cmd;
                cmd.key = extractString(args[0], "Config.setBool");
                cmd.value = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_config_getBool",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);
                events::save::GetConfigBoolQuery query;
                query.key = extractString(args[0], "Config.getBool");
                query.defaultValue = extractBool(args[1]);
                return value::Value(dispatcher.query(query));
            }});
    }
}
