// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptSceneEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneManagementEvents.hpp"

#include "print/Log.hpp"

namespace core
{
    ScriptSceneEventBridge::ScriptSceneEventBridge(
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

    void ScriptSceneEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::scene::SceneLoadingCompletedNotification>(
            [this](const ::events::scene::SceneLoadingCompletedNotification& notif)
            {
                dispatchSceneCallback("onSceneLoaded", notif.scenePath);
                resolveAsyncCallback(notif.scenePath, notif.success);
            }));

        tokens.push_back(dispatcher.subscribe<::events::scene::SceneClearedNotification>(
            [this](const ::events::scene::SceneClearedNotification&)
            {
                dispatchSceneCallback("onSceneUnloaded", "");
            }));

        tokens.push_back(dispatcher.subscribe<::events::scene::AdditiveSceneLoadedNotification>(
            [this](const ::events::scene::AdditiveSceneLoadedNotification& notif)
            {
                dispatchSceneCallback("onSceneLoaded", notif.sceneName);
            }));

        tokens.push_back(dispatcher.subscribe<::events::scene::AdditiveSceneUnloadedNotification>(
            [this](const ::events::scene::AdditiveSceneUnloadedNotification& notif)
            {
                dispatchSceneCallback("onSceneUnloaded", notif.sceneName);
            }));

        vfLogInfo("[ScriptSceneEventBridge] Subscribed to scene events");
    }

    void ScriptSceneEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        {
            std::lock_guard<std::mutex> lock(asyncCallbackMutex);
            asyncCallbacks.clear();
        }

        vfLogInfo("[ScriptSceneEventBridge] Unsubscribed from scene events");
    }

    void ScriptSceneEventBridge::storeAsyncCallback(const std::string& scenePath, const value::Value& callback)
    {
        std::lock_guard<std::mutex> lock(asyncCallbackMutex);
        asyncCallbacks[scenePath] = callback;
    }

    void ScriptSceneEventBridge::dispatchSceneCallback(const char* methodName, const std::string& sceneName)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find("ISceneEventListener") == interfaces.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt != instanceToEntity.end())
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
            }

            try
            {
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, {value::Value(sceneName)});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptSceneEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }

    void ScriptSceneEventBridge::resolveAsyncCallback(const std::string& scenePath, bool success)
    {
        value::Value callback;
        {
            std::lock_guard<std::mutex> lock(asyncCallbackMutex);
            auto it = asyncCallbacks.find(scenePath);
            if (it == asyncCallbacks.end()) return;
            callback = it->second;
            asyncCallbacks.erase(it);
        }

        try
        {
            interpreter->callLambda(callback, {value::Value(success), value::Value(scenePath)});
        }
        catch (const std::exception& e)
        {
            vfLogWarning("[ScriptSceneEventBridge] Async callback error for '{}': {}", scenePath, e.what());
        }
    }
}
