// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "InputActionAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/ActionMappingEvents.hpp"
#include "../../../services/data/ActionMappingTypes.hpp"

namespace core::api
{
    void InputActionAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_inputaction_isDown(actionName) -> bool
        interpreter->registerNativeFunction("_native_inputaction_isDown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                std::string name = extractString(args[0], "_native_inputaction_isDown");

                events::input::IsActionDownQuery query;
                query.actionName = name;
                return value::Value(dispatcher.query(query));
            });

        // _native_inputaction_isPressed(actionName) -> bool
        interpreter->registerNativeFunction("_native_inputaction_isPressed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                std::string name = extractString(args[0], "_native_inputaction_isPressed");

                events::input::IsActionPressedQuery query;
                query.actionName = name;
                return value::Value(dispatcher.query(query));
            });

        // _native_inputaction_isReleased(actionName) -> bool
        interpreter->registerNativeFunction("_native_inputaction_isReleased",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                std::string name = extractString(args[0], "_native_inputaction_isReleased");

                events::input::IsActionReleasedQuery query;
                query.actionName = name;
                return value::Value(dispatcher.query(query));
            });

        // _native_inputaction_register(actionName, bindingType, code) -> void
        interpreter->registerNativeFunction("_native_inputaction_register",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaction_register");
                int type = static_cast<int>(extractInt64(args[1]));
                int code = static_cast<int>(extractInt64(args[2]));

                services::InputBinding binding;
                binding.type = static_cast<services::BindingType>(type);
                binding.code = code;
                binding.requireShift = args.size() > 3 ? extractBool(args[3]) : false;
                binding.requireCtrl = args.size() > 4 ? extractBool(args[4]) : false;
                binding.requireAlt = args.size() > 5 ? extractBool(args[5]) : false;

                events::input::RegisterActionCommand cmd;
                cmd.actionName = name;
                cmd.defaultBindings.push_back(binding);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaction_addBinding(actionName, bindingType, code, [shift, ctrl, alt]) -> void
        interpreter->registerNativeFunction("_native_inputaction_addBinding",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaction_addBinding");
                int type = static_cast<int>(extractInt64(args[1]));
                int code = static_cast<int>(extractInt64(args[2]));

                services::InputBinding binding;
                binding.type = static_cast<services::BindingType>(type);
                binding.code = code;
                binding.requireShift = args.size() > 3 ? extractBool(args[3]) : false;
                binding.requireCtrl = args.size() > 4 ? extractBool(args[4]) : false;
                binding.requireAlt = args.size() > 5 ? extractBool(args[5]) : false;

                events::input::AddActionBindingCommand cmd;
                cmd.actionName = name;
                cmd.binding = binding;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaction_removeBinding(actionName, bindingType, code, [shift, ctrl, alt]) -> void
        interpreter->registerNativeFunction("_native_inputaction_removeBinding",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaction_removeBinding");
                int type = static_cast<int>(extractInt64(args[1]));
                int code = static_cast<int>(extractInt64(args[2]));

                services::InputBinding binding;
                binding.type = static_cast<services::BindingType>(type);
                binding.code = code;
                binding.requireShift = args.size() > 3 ? extractBool(args[3]) : false;
                binding.requireCtrl = args.size() > 4 ? extractBool(args[4]) : false;
                binding.requireAlt = args.size() > 5 ? extractBool(args[5]) : false;

                events::input::RemoveActionBindingCommand cmd;
                cmd.actionName = name;
                cmd.binding = binding;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaction_resetBindings(actionName) -> void
        interpreter->registerNativeFunction("_native_inputaction_resetBindings",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaction_resetBindings");

                events::input::ResetActionBindingsCommand cmd;
                cmd.actionName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaction_save(filePath) -> bool
        interpreter->registerNativeFunction("_native_inputaction_save",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                std::string path = extractString(args[0], "_native_inputaction_save");

                events::input::SaveActionBindingsCommand cmd;
                cmd.filePath = path;
                return value::Value(dispatcher.execute(cmd));
            });

        // _native_inputaction_load(filePath) -> bool
        interpreter->registerNativeFunction("_native_inputaction_load",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(false);
                std::string path = extractString(args[0], "_native_inputaction_load");

                events::input::LoadActionBindingsCommand cmd;
                cmd.filePath = path;
                return value::Value(dispatcher.execute(cmd));
            });
    }
}
