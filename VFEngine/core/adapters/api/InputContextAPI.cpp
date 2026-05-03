// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "InputContextAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/InputContextEvents.hpp"

namespace core::api
{
    void InputContextAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_inputcontext_create(name, blocking) -> void
        interpreter->registerNativeFunction("_native_inputcontext_create",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputcontext_create");
                bool blocking = args.size() > 1 ? extractBool(args[1]) : true;

                events::input::CreateContextCommand cmd;
                cmd.contextName = name;
                cmd.blocking = blocking;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_inputcontext_remove(name) -> void
        interpreter->registerNativeFunction("_native_inputcontext_remove",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputcontext_remove");

                events::input::RemoveContextCommand cmd;
                cmd.contextName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_inputcontext_push(name) -> void
        interpreter->registerNativeFunction("_native_inputcontext_push",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputcontext_push");

                events::input::PushContextCommand cmd;
                cmd.contextName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_inputcontext_pop(name) -> void
        interpreter->registerNativeFunction("_native_inputcontext_pop",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                std::string name = args.empty() ? "" : extractString(args[0], "_native_inputcontext_pop");

                events::input::PopContextCommand cmd;
                cmd.contextName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_inputcontext_isActive(name) -> bool
        interpreter->registerNativeFunction("_native_inputcontext_isActive",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                std::string name = extractString(args[0], "_native_inputcontext_isActive");

                events::input::IsContextActiveQuery query;
                query.contextName = name;
                return value::Value(dispatcher.query(query));
            }});

        // _native_inputcontext_setBlocking(name, blocking) -> void
        interpreter->registerNativeFunction("_native_inputcontext_setBlocking",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputcontext_setBlocking");
                bool blocking = extractBool(args[1]);

                events::input::SetContextBlockingCommand cmd;
                cmd.contextName = name;
                cmd.blocking = blocking;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});
    }
}
