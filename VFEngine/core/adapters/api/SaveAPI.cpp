// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "SaveAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/save/SaveEvents.hpp"
#include <nlohmann/json.hpp>

namespace core::api
{
    void SaveAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_save_createSlot(name) -> void
        interpreter->registerNativeFunction("_native_save_createSlot",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Save.createSlot: missing name argument");
                    return value::Value(std::monostate{});
                }
                std::string name = extractString(args[0], "Save.createSlot");

                events::save::CreateSaveSlotCommand cmd;
                cmd.slotName = name;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_save_save(slotName) -> void
        interpreter->registerNativeFunction("_native_save_save",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Save.save: missing slotName argument");
                    return value::Value(std::monostate{});
                }
                std::string slotName = extractString(args[0], "Save.save");

                events::save::SaveGameCommand cmd;
                cmd.slotName = slotName;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_save_load(slotName) -> void
        interpreter->registerNativeFunction("_native_save_load",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Save.load: missing slotName argument");
                    return value::Value(std::monostate{});
                }
                std::string slotName = extractString(args[0], "Save.load");

                events::save::LoadGameCommand cmd;
                cmd.slotName = slotName;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_save_listSlots() -> array of strings
        interpreter->registerNativeFunction("_native_save_listSlots",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::save::ListSaveSlotsQuery query;
                auto slots = dispatcher.query(query);

                auto arr = std::make_shared<value::NativeArray>(
                    static_cast<int>(slots.size()), value::ValueType::STRING);
                for (size_t i = 0; i < slots.size(); ++i)
                {
                    arr->set(i, value::Value(slots[i]));
                }
                return value::Value(arr);
            }});

        // _native_save_delete(slotName) -> void
        interpreter->registerNativeFunction("_native_save_delete",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    vfLogError("[Script] Save.delete: missing slotName argument");
                    return value::Value(std::monostate{});
                }
                std::string slotName = extractString(args[0], "Save.delete");

                events::save::DeleteSaveSlotCommand cmd;
                cmd.slotName = slotName;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            }});

        // _native_save_getMetadata(slotName) -> string (JSON)
        interpreter->registerNativeFunction("_native_save_getMetadata",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(std::string("{}"));
                }
                std::string slotName = extractString(args[0], "Save.getMetadata");

                events::save::GetSaveMetadataQuery query;
                query.slotName = slotName;
                auto metadata = dispatcher.query(query);

                // Return as JSON string
                nlohmann::json j;
                j["slotName"] = metadata.slotName;
                j["timestamp"] = metadata.timestamp;
                j["playtimeSeconds"] = metadata.playtimeSeconds;
                j["customData"] = metadata.customData;
                return value::Value(j.dump());
            }});
    }
}
