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
        std::unordered_map<uint64_t, std::any>& instanceToObject,
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

        // Combo-sequence cues (VK-1495). Published only by forward playback
        // (update -> publishNewlyFiredMarkers) and manual triggerCue; seek/scrub/
        // replay/prewarm route through replayTo() and never publish, so no
        // transport guard is needed here.
        tokens.push_back(dispatcher.subscribe<::services::events::vfxsequence::VFXComboCueFiredNotification>(
            [this](const ::services::events::vfxsequence::VFXComboCueFiredNotification& notif)
            {
                dispatchComboCue(notif.comboId, notif.cueName, notif.payload);
            }));

        vfLogInfo("[ScriptVFXEventBridge] Subscribed to VFX particle events and combo cues");
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
                interfaceIt->second.find(kVFXEventListener) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                AmbientScriptContext ctx(entityHandle, instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
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

    void ScriptVFXEventBridge::dispatchComboCue(
        uint32_t comboId, const std::string& cueName, const ::vfx::VFXCuePayload& payload)
    {
        // The payload fields are std::optional at the source; the has* flags
        // preserve "unset" vs a real zero so scripts can tell them apart. Guard
        // every optional before dereferencing (dereferencing an empty one is UB).
        const bool hasPos = payload.position.has_value();
        const glm::vec3 pos = hasPos ? *payload.position : glm::vec3(0.0f);
        const bool hasCol = payload.color.has_value();
        const glm::vec4 col = hasCol ? *payload.color : glm::vec4(0.0f);
        const bool hasScl = payload.scalar.has_value();
        const float scl = payload.scalar.value_or(0.0f);

        // Build the payload object once; it is never mutated, so it is safe to
        // reuse across every listener instance (mirrors positionObj above).
        value::Value payloadObj = interpreter->createObject("ComboCuePayload",
            {value::Value(hasPos), value::Value(pos.x), value::Value(pos.y), value::Value(pos.z),
             value::Value(hasCol), value::Value(col.r), value::Value(col.g), value::Value(col.b),
             value::Value(col.a),
             value::Value(hasScl), value::Value(scl)});

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find(kVFXComboCueListener) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                AmbientScriptContext ctx(entityHandle, instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onComboCue",
                                        {value::Value(static_cast<int>(comboId)),
                                         value::Value(cueName),
                                         payloadObj});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptVFXEventBridge] onComboCue callback error: {}", e.what());
            }
        }
    }
}
