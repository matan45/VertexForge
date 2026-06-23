// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <value/ValueShim.hpp>
#include <value/ObjectInstance.hpp>
#include <environment/registry/ClassDefinition.hpp>

#include "ScriptingAdapter.hpp"
#include "CoroutineManager.hpp"
#include "NativeAPIRegistry.hpp"
#include <vm/runtime/VirtualMachine.hpp>
#include <runtime/EventLoop.hpp>

#include "time/Timer.hpp"
#include "print/Log.hpp"

namespace core
{
    namespace
    {
        void callScriptMethod(::services::ScriptInterpreter* interpreter,
                              std::unordered_map<uint64_t, std::any>& instanceToObject,
                              std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
                              uint64_t instanceId, const char* methodName,
                              const std::vector<value::Value>& args)
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) return;

            NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
            NativeAPIRegistry::setCurrentInstanceId(instanceId);
            auto& instance = std::any_cast<value::Value&>(objIt->second);
            interpreter->callMethod(instance, methodName, args);
        }
    }

    void ScriptingAdapter::callOnStart(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] callOnStart: script {} not loaded", instanceId);
            return;
        }

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onStart", {});
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onStart failed: ") + e.what());
            vfLogError("[Script] onStart failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnUpdate(uint64_t instanceId, float deltaTime)
    {
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onUpdate", {value::Value(static_cast<double>(deltaTime))});
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            vfLogError("[Script] onUpdate failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnFixedUpdate(uint64_t instanceId, float fixedDeltaTime)
    {
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onFixedUpdate", {value::Value(fixedDeltaTime)});
        }
        catch (const std::exception&) {}
    }

    void ScriptingAdapter::callOnLateUpdate(uint64_t instanceId, float deltaTime)
    {
        if (!isScriptLoaded(instanceId)) return;

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
            return;

        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onLateUpdate", {value::Value(deltaTime)});
        }
        catch (const std::exception&) {}
    }

    void ScriptingAdapter::callOnEnable(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;
        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onEnable", {});
        }
        catch (const std::exception&) {}
    }

    void ScriptingAdapter::callOnDisable(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;
        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onDisable", {});
        }
        catch (const std::exception&) {}
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId)) return;
        try
        {
            callScriptMethod(interpreter.get(), instanceToObject, instanceToEntity, instanceId, "onDestroy", {});
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            vfLogError("[Script] onDestroy failed: {}", e.what());
        }
    }

    void ScriptingAdapter::tickCoroutines(float deltaTime)
    {
        if (coroutineManager)
        {
            // Use Timer's scaled (game) and raw deltas directly so Seconds waits
            // pause on freeze / honor time-scale while RealSeconds waits do not.
            // The forwarded deltaTime param is intentionally ignored for coroutine
            // bookkeeping; Timer is the single source of truth here.
            coroutineManager->tickFrame(engineTime::Timer::getGameDeltaTime(),
                                        engineTime::Timer::getDeltaTime());
        }

        auto vm = interpreter->getVM();
        if (vm)
        {
            auto* eventLoop = vm->getEventLoop();
            if (eventLoop)
            {
                int budget = 64;
                while (budget-- > 0 && eventLoop->tick()) {}
            }
        }
    }

    void ScriptingAdapter::tickFixedUpdateCoroutines()
    {
        if (coroutineManager)
            coroutineManager->tickFixedUpdate();

        auto vm = interpreter->getVM();
        if (vm)
        {
            auto* eventLoop = vm->getEventLoop();
            if (eventLoop)
            {
                int budget = 64;
                while (budget-- > 0 && eventLoop->tick()) {}
            }
        }
    }

    std::string ScriptingAdapter::callMethodWithReturn(uint64_t instanceId, const std::string& methodName,
                                                       const std::vector<std::any>& args)
    {
        if (!isScriptLoaded(instanceId)) return "";

        auto objIt = instanceToObject.find(instanceId);
        if (objIt == instanceToObject.end()) return "";

        try
        {
            NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
            auto& instance = std::any_cast<value::Value&>(objIt->second);

            std::vector<value::Value> valueArgs;
            for (const auto& arg : args)
            {
                if (arg.type() == typeid(float))
                    valueArgs.emplace_back(static_cast<double>(std::any_cast<float>(arg)));
                else if (arg.type() == typeid(double))
                    valueArgs.emplace_back(std::any_cast<double>(arg));
                else if (arg.type() == typeid(int))
                    valueArgs.emplace_back(static_cast<int64_t>(std::any_cast<int>(arg)));
                else if (arg.type() == typeid(int64_t))
                    valueArgs.emplace_back(std::any_cast<int64_t>(arg));
                else if (arg.type() == typeid(bool))
                    valueArgs.emplace_back(std::any_cast<bool>(arg));
                else if (arg.type() == typeid(std::string))
                    valueArgs.emplace_back(std::any_cast<std::string>(arg));
            }

            auto result = interpreter->callMethod(instance, methodName, valueArgs);

            if (value::isString(result))
                return value::asString(result);
            if (value::isBool(result))
                return value::asBool(result) ? "success" : "failure";

            return "";
        }
        catch (const std::exception& e)
        {
            vfLogError("[Script] {} failed: {}", methodName, e.what());
            return "";
        }
    }

    bool ScriptingAdapter::hasMethod(uint64_t instanceId, const std::string& methodName) const
    {
        if (!isScriptLoaded(instanceId)) return false;

        auto objIt = instanceToObject.find(instanceId);
        if (objIt == instanceToObject.end()) return false;

        try
        {
            const auto& instance = std::any_cast<const value::Value&>(objIt->second);
            if (!value::isObject(instance)) return false;

            auto classDef = value::asObject(instance)->getClassDefinition();
            return classDef && classDef->findInstanceMethodInHierarchy(methodName, 0) != nullptr;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    void ScriptingAdapter::playVFX(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] playScript: script {} not loaded", instanceId);
            return;
        }

        auto& state = instanceToPlaybackState[instanceId];
        if (state == services::ScriptPlaybackState::Stopped)
            callOnStart(instanceId);
        state = services::ScriptPlaybackState::Playing;
        vfLogInfo("[ScriptingAdapter] Script {} now playing", instanceId);
    }

    void ScriptingAdapter::setInstancePriority(uint64_t instanceId, int priority)
    {
        instanceToPriority[instanceId] = priority;
    }
}
