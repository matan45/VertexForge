// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptVFXEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "../../services/events/vfx/VFXEventNotifications.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptVFXEventBridge::ScriptVFXEventBridge(
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

    void ScriptVFXEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::services::events::vfxruntime::VFXParticleEventNotification>(
            [this](const ::services::events::vfxruntime::VFXParticleEventNotification& notif)
            {
                dispatchVFXEvent(notif.eventType,
                                 notif.position.x, notif.position.y, notif.position.z,
                                 notif.velocity.x, notif.velocity.y, notif.velocity.z,
                                 notif.entityId);
            }));

        vfLogInfo("[ScriptVFXEventBridge] Subscribed to VFX particle events");
    }

    void ScriptVFXEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptVFXEventBridge] Unsubscribed from VFX particle events");
    }

    void ScriptVFXEventBridge::dispatchVFXEvent(
        uint32_t eventType,
        float posX, float posY, float posZ,
        float velX, float velY, float velZ,
        uint32_t entityId)
    {
        value::Value positionObj = interpreter->createObject("Vec3f",
            {value::Value(posX), value::Value(posY), value::Value(posZ)});
        value::Value velocityObj = interpreter->createObject("Vec3f",
            {value::Value(velX), value::Value(velY), value::Value(velZ)});

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find("IVFXEventListener") == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(entityHandle);
                auto& instance = std::any_cast<value::Value&>(const_cast<std::any&>(objIt->second));
                interpreter->callMethod(instance, "onVFXParticleEvent",
                                        {value::Value(static_cast<int>(eventType)),
                                         positionObj,
                                         velocityObj,
                                         value::Value(static_cast<int>(entityId))});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptVFXEventBridge] onVFXParticleEvent callback error: {}", e.what());
            }
        }
    }
}
