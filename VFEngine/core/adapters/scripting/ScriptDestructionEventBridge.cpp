// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptDestructionEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "../api/NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/destruction/DestructionEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include "print/Log.hpp"

namespace core
{
    namespace
    {
        void dispatchToEntityListeners(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
            const char* methodName,
            ::services::EntityHandle entity,
            const std::vector<value::Value>& args)
        {
            for (const auto& [instanceId, entityHandle] : instanceToEntity)
            {
                if (entityHandle.id != entity.id) continue;

                auto interfaceIt = instanceToInterfaces.find(instanceId);
                if (interfaceIt == instanceToInterfaces.end() ||
                    interfaceIt->second.find(kDestructionListener) == interfaceIt->second.end())
                    continue;

                auto objIt = instanceToObject.find(instanceId);
                if (objIt == instanceToObject.end()) continue;

                try
                {
                    AmbientScriptContext ctx(entity, instanceId);
                    auto& instance = std::any_cast<value::Value&>(objIt->second);
                    interpreter->callMethod(instance, methodName, args);
                }
                catch (const std::exception& e)
                {
                    vfLogWarning("[ScriptDestructionEventBridge] {} error: {}", methodName, e.what());
                }
            }
        }
    }

    ScriptDestructionEventBridge::ScriptDestructionEventBridge(
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

    void ScriptDestructionEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::destruction::DamageAppliedNotification>(
            [this](const ::events::destruction::DamageAppliedNotification& notif)
            {
                std::vector<value::Value> args;
                args.push_back(value::Value(static_cast<double>(notif.damageAmount)));
                args.push_back(value::Value(static_cast<double>(notif.remainingHealth)));
                args.push_back(api::makeVec3Array(notif.impactPoint));
                args.push_back(api::makeVec3Array(notif.impactDirection));
                args.push_back(value::Value(static_cast<int64_t>(notif.damageType)));

                dispatchToEntityListeners(interpreter, instanceToInterfaces, instanceToObject,
                                          instanceToEntity, "onDamageReceived", notif.entity, args);
            }));

        tokens.push_back(dispatcher.subscribe<::events::destruction::DestructionTriggeredNotification>(
            [this](const ::events::destruction::DestructionTriggeredNotification& notif)
            {
                std::vector<value::Value> args;
                args.push_back(api::makeVec3Array(notif.impactPoint));
                args.push_back(api::makeVec3Array(notif.impactDirection));

                dispatchToEntityListeners(interpreter, instanceToInterfaces, instanceToObject,
                                          instanceToEntity, "onDestroyed", notif.entity, args);
            }));

        tokens.push_back(dispatcher.subscribe<::events::destruction::FragmentDetachedNotification>(
            [this](const ::events::destruction::FragmentDetachedNotification& notif)
            {
                std::vector<value::Value> args;
                args.push_back(value::Value(static_cast<int64_t>(notif.fragmentEntity.id)));
                args.push_back(value::Value(static_cast<int64_t>(notif.fragmentIndex)));

                dispatchToEntityListeners(interpreter, instanceToInterfaces, instanceToObject,
                                          instanceToEntity, "onFragmentCollision", notif.sourceEntity, args);
            }));

        vfLogInfo("[ScriptDestructionEventBridge] Subscribed to destruction events");
    }

    void ScriptDestructionEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            dispatcher.unsubscribe(token);
        }
        tokens.clear();
    }
}
