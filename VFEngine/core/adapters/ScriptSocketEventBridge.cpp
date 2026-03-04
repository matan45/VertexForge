// mType headers must come first to avoid Windows macro conflicts
#include "print/Log.hpp"
#include <services/ScriptInterpreter.hpp>

#include "ScriptSocketEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/SocketEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core
{
    ScriptSocketEventBridge::ScriptSocketEventBridge(
        ::services::ScriptInterpreter* interpreter,
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
        const std::unordered_map<uint64_t, std::any>& instanceToObject,
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity)
        : interpreter(interpreter)
        , instanceToInterfaces(instanceToInterfaces)
        , instanceToObject(instanceToObject)
        , instanceToEntity(instanceToEntity)
    {
    }

    void ScriptSocketEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::socket::SocketAttachmentChangedNotification>(
            [this](const ::events::socket::SocketAttachmentChangedNotification& notif)
            {
                dispatchSocketEvent(notif.childEntity, notif.parentEntity,
                                    notif.socketName, notif.attached);
            }));

        vfLogInfo("[ScriptSocketEventBridge] Subscribed to socket attachment events");
    }

    void ScriptSocketEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptSocketEventBridge] Unsubscribed from socket attachment events");
    }

    void ScriptSocketEventBridge::dispatchSocketEvent(
        ::services::EntityHandle childEntity,
        ::services::EntityHandle parentEntity,
        const std::string& socketName,
        bool attached)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        const char* methodName = attached ? "onSocketAttached" : "onSocketDetached";

        // Dispatch to scripts on both the parent and child entities
        ::services::EntityHandle targetEntities[] = {childEntity, parentEntity};

        for (const auto& targetEntity : targetEntities)
        {
            auto entity = static_cast<entt::entity>(targetEntity.id);
            if (!registry.valid(entity) ||
                !registry.all_of<components::ScriptComponent>(entity))
                continue;

            for (const auto& [instanceId, entityHandle] : instanceToEntity)
            {
                if (entityHandle.id != targetEntity.id) continue;

                auto interfaceIt = instanceToInterfaces.find(instanceId);
                if (interfaceIt == instanceToInterfaces.end() ||
                    interfaceIt->second.find("ISocketAttachmentListener") == interfaceIt->second.end())
                    continue;

                auto objIt = instanceToObject.find(instanceId);
                if (objIt == instanceToObject.end()) continue;

                try
                {
                    NativeAPIRegistry::setCurrentEntity(targetEntity);
                    auto& instance = std::any_cast<value::Value&>(const_cast<std::any&>(objIt->second));
                    // Args: (childId, parentId, socketName)
                    interpreter->callMethod(instance, methodName,
                                            {value::Value(static_cast<int64_t>(childEntity.id)),
                                             value::Value(static_cast<int64_t>(parentEntity.id)),
                                             value::Value(socketName)});
                }
                catch (const std::exception& e)
                {
                    vfLogWarning("[ScriptSocketEventBridge] {} callback error: {}", methodName, e.what());
                }
            }
        }
    }
}
