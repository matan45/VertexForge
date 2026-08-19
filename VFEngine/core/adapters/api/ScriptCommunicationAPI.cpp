// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "ScriptCommunicationAPI.hpp"
#include "NativeHelpers.hpp"
#include "../scripting/NativeAPIRegistry.hpp"
#include "../scripting/ScriptCommunicationManager.hpp"

namespace core::api
{
    void ScriptCommunicationAPI::setManager(ScriptCommunicationManager* manager)
    {
        communicationManager = manager;
    }

    void ScriptCommunicationAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_entity_getScript",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Entity.getScript");
                std::string className = extractString(args[1], "Entity.getScript");
                if (entityId < 0 || className.empty())
                    return value::Value(std::monostate{});

                return communicationManager->getScript(static_cast<uint64_t>(entityId), className);
            }});

        interpreter->registerNativeFunction("_native_entity_hasScript",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(false);

                int64_t entityId = extractInt64(args[0], "Entity.hasScriptOfType");
                std::string className = extractString(args[1], "Entity.hasScriptOfType");
                if (entityId < 0 || className.empty())
                    return value::Value(false);

                return value::Value(communicationManager->hasScript(
                    static_cast<uint64_t>(entityId), className));
            }});

        // sendMessage(entityId, callback) — calls callback(scriptObject) for each script on entity
        interpreter->registerNativeFunction("_native_entity_sendMessage",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Entity.sendMessage");
                if (entityId < 0)
                    return value::Value(std::monostate{});

                // args[1] is the lambda callback
                const auto& callback = args[1];

                // Extra args beyond entityId and callback are forwarded
                std::vector<value::Value> forwardArgs(args.begin() + 2, args.end());
                communicationManager->sendMessage(
                    static_cast<uint64_t>(entityId), callback, forwardArgs);
                return value::Value(std::monostate{});
            }});

        // broadcastMessage(entityId, callback) — calls callback(scriptObject) for each script on entity and descendants
        interpreter->registerNativeFunction("_native_entity_broadcastMessage",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(std::monostate{});

                int64_t entityId = extractInt64(args[0], "Entity.broadcastMessage");
                if (entityId < 0)
                    return value::Value(std::monostate{});

                const auto& callback = args[1];

                std::vector<value::Value> forwardArgs(args.begin() + 2, args.end());
                communicationManager->broadcastMessage(
                    static_cast<uint64_t>(entityId), callback, forwardArgs);
                return value::Value(std::monostate{});
            }});

        // listen(eventName, callback) — registers lambda to be called when event fires
        interpreter->registerNativeFunction("_native_scriptEvent_listen",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(static_cast<int64_t>(-1));

                std::string eventName = extractString(args[0], "ScriptEvent.listen");
                if (eventName.empty())
                    return value::Value(static_cast<int64_t>(-1));

                // args[1] is the lambda callback
                const auto& callback = args[1];

                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                uint64_t token = communicationManager->listen(eventName, instanceId, callback,
                                                              /*wantsPayload=*/false);
                return value::Value(static_cast<int64_t>(token));
            }});

        // listenJson(eventName, callback) — same subscription list as listen(), but the
        // callback takes one string argument. Plugin events published on PluginEventBus carry
        // a JSON object; it arrives here as its serialized form. A separate native is needed
        // because the VM rejects any arity mismatch, so a 1-arg callback cannot share a
        // registration path with EventCallback's 0-arg invoke().
        interpreter->registerNativeFunction("_native_scriptEvent_listenJson",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.size() < 2)
                    return value::Value(static_cast<int64_t>(-1));

                std::string eventName = extractString(args[0], "ScriptEvent.listenJson");
                if (eventName.empty())
                    return value::Value(static_cast<int64_t>(-1));

                const auto& callback = args[1];

                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                uint64_t token = communicationManager->listen(eventName, instanceId, callback,
                                                              /*wantsPayload=*/true);
                return value::Value(static_cast<int64_t>(token));
            }});

        interpreter->registerNativeFunction("_native_scriptEvent_unlisten",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.empty())
                    return value::Value(std::monostate{});

                int64_t token = extractInt64(args[0], "ScriptEvent.unlisten");
                if (token > 0)
                    communicationManager->unlisten(static_cast<uint64_t>(token));

                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_scriptEvent_emit",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!communicationManager || args.empty())
                    return value::Value(std::monostate{});

                std::string eventName = extractString(args[0], "ScriptEvent.emit");
                if (eventName.empty())
                    return value::Value(std::monostate{});

                // Extra args beyond eventName are forwarded to listener callbacks
                std::vector<value::Value> forwardArgs(args.begin() + 1, args.end());
                communicationManager->emit(eventName, forwardArgs);
                return value::Value(std::monostate{});
            }});
    }
}
