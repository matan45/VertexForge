#pragma once
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/EventTypes.hpp"
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <utility>
#include <vector>
#include <any>

namespace services
{
    class ScriptInterpreter;
}

namespace plugin
{
    struct PluginNativeBinding;   // mType plugin C-ABI binding (plugin/PluginHost.hpp)
}

namespace core
{
    class ScriptUIEventBridge;
    class ScriptPhysicsEventBridge;
    class ScriptAnimationEventBridge;
    class ScriptSocketEventBridge;
    class ScriptVFXEventBridge;
    class ScriptNavigationEventBridge;
    class ScriptInputActionEventBridge;
    class ScriptSceneEventBridge;
    class ScriptWeatherEventBridge;
    class ScriptDestructionEventBridge;
    class ScriptOceanEventBridge;

    class CoroutineManager;
    class ScriptCommunicationManager;
    class ScriptDebugServer;

    class ScriptingAdapter : public ::services::IScriptingProvider
    {
    private:
        std::unique_ptr<::services::ScriptInterpreter> interpreter;
        std::unique_ptr<NativeAPIRegistry> apiRegistry;
        std::unique_ptr<CoroutineManager> coroutineManager;
        std::unique_ptr<ScriptUIEventBridge> uiEventBridge;
        std::unique_ptr<ScriptPhysicsEventBridge> physicsEventBridge;
        std::unique_ptr<ScriptAnimationEventBridge> animationEventBridge;
        std::unique_ptr<ScriptSocketEventBridge> socketEventBridge;
        std::unique_ptr<ScriptVFXEventBridge> vfxEventBridge;
        std::unique_ptr<ScriptNavigationEventBridge> navigationEventBridge;
        std::unique_ptr<ScriptInputActionEventBridge> inputActionEventBridge;
        std::unique_ptr<ScriptSceneEventBridge> sceneEventBridge;
        std::unique_ptr<ScriptWeatherEventBridge> weatherEventBridge;
        std::unique_ptr<ScriptDestructionEventBridge> destructionEventBridge;
        std::unique_ptr<ScriptOceanEventBridge> oceanEventBridge;
        std::unique_ptr<ScriptCommunicationManager> communicationManager;
        std::unique_ptr<ScriptDebugServer> debugServer;

        std::unordered_map<uint64_t, std::string> instanceToClassName;
        std::unordered_map<uint64_t, ::services::EntityHandle> instanceToEntity;
        std::unordered_map<uint64_t, std::any> instanceToObject;
        std::unordered_map<std::string, std::string> pathToClassName;
        std::unordered_map<uint64_t, std::unordered_set<std::string>> instanceToInterfaces;
        std::unordered_map<uint64_t, ::services::ScriptPlaybackState> instanceToPlaybackState;
        std::unordered_map<uint64_t, int> instanceToPriority;

        // Union of every event bridge's kRequiredInterfaces, aggregated in
        // init(). loadScript probes exactly this set when caching a class's
        // implemented interfaces — a bridge that dispatches on an interface
        // missing from its own kRequiredInterfaces silently never fires
        // (test_script_listener_coverage guards that invariant).
        std::unordered_set<std::string> checkedInterfaces;

        // Engine-plugin script natives registered via the mType C ABI: the
        // bindings own the {fn, userData} pair the host trampoline dereferences
        // on every call, so they must outlive the registration (erased on
        // unregisterPluginNativeFunction / cleanUp).
        std::unordered_map<std::string, std::unique_ptr<::plugin::PluginNativeBinding>> pluginNativeBindings;

        // PluginEventBus -> mType bridge. The bus runs its handlers inline on whichever thread
        // published (a plugin update task, the main thread, ...), so the handler only appends
        // {eventName, json.dump()} here; pumpPluginEvents() drains it on the script thread.
        // A deque because nothing drains it while scripts are stopped (Edit mode) or game time
        // is frozen, so the oldest entries are dropped once it saturates.
        std::mutex pluginEventMutex;
        std::deque<std::pair<std::string, std::string>> pendingPluginEvents;
        std::vector<::events::SubscriptionToken> pluginEventTokens;
        std::unordered_set<std::string> bridgedPluginEvents;

        mutable std::optional<::services::ScriptError> lastError;

        uint64_t nextInstanceId = 1;
        std::string scriptLibraryPath;
        bool initialized = false;
        bool compiled = false;

    public:
        explicit ScriptingAdapter();
        ~ScriptingAdapter() override;

        ScriptingAdapter(const ScriptingAdapter&) = delete;
        ScriptingAdapter& operator=(const ScriptingAdapter&) = delete;

        // === VM Lifecycle ===
        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        // === Script Building ===
        ::services::ScriptBuildResult buildScripts(const std::string& manifestPath) override;
        void cleanScripts(const std::string& manifestPath) override;
        bool isCompiled() const override;
        bool loadCompiledScripts(const std::string& manifestPath) override;

        // === Script Loading ===
        std::optional<::services::ScriptInstanceInfo> loadScript(
            const std::string& scriptPath,
            ::services::EntityHandle entity) override;
        void unloadScript(uint64_t instanceId) override;
        void unloadAllScripts() override;
        bool isScriptLoaded(uint64_t instanceId) const override;

        // === Lifecycle Calls ===
        void callOnStart(uint64_t instanceId) override;
        void callOnUpdate(uint64_t instanceId, float deltaTime) override;
        void callOnFixedUpdate(uint64_t instanceId, float fixedDeltaTime) override;
        void callOnLateUpdate(uint64_t instanceId, float deltaTime) override;
        void callOnEnable(uint64_t instanceId) override;
        void callOnDisable(uint64_t instanceId) override;
        void callOnDestroy(uint64_t instanceId) override;

        void tickCoroutines(float deltaTime) override;
        void tickFixedUpdateCoroutines() override;

        void pumpPluginEvents() override;

        std::string callMethodWithReturn(uint64_t instanceId, const std::string& methodName,
                                          const std::vector<std::any>& args = {}) override;

        bool hasMethod(uint64_t instanceId, const std::string& methodName) const override;

        void playVFX(uint64_t instanceId) override;
        void setInstancePriority(uint64_t instanceId, int priority) override;

        std::optional<::services::ScriptError> getLastError() const override;
        void clearError() override;

        void setScriptLibraryPath(const std::string& path) override;

        // === Debugger (VK-1371) ===
        void startDebugServer(int port) override;
        void stopDebugServer() override;
        bool isDebuggerActive() const override;

        void registerPluginNativeFunction(const std::string& name, std::any function) override;
        void unregisterPluginNativeFunction(const std::string& name) override;

        // === Save/Load State ===
        std::string getInstanceState(uint64_t instanceId) override;
        bool setInstanceState(uint64_t instanceId, const std::string& jsonState) override;
        bool isSaveableInstance(uint64_t instanceId) const override;
        std::vector<uint64_t> getAllInstanceIds() const override;
        ::services::EntityHandle getInstanceEntity(uint64_t instanceId) const override;
        std::string getInstanceClassName(uint64_t instanceId) const override;
        std::string getInstanceScriptPath(uint64_t instanceId) const override;

    private:
        void setError(::services::ScriptError::Type type, const std::string& message,
                      const std::string& file = "", int line = 0);

        std::string extractClassName(const std::string& scriptPath);
        std::string getLibraryPath(const std::string& manifestPath) const;

        // VK-1458 OOP layer: true when className's inheritance chain reaches
        // the script-side Behaviour base class.
        bool classExtendsBehaviour(const std::string& className) const;

        // Writes the instance's entity id into Behaviour.vfEntityId (gated on
        // classExtendsBehaviour). Called after createObject and again after a
        // @Saveable state restore, which would otherwise clobber the live id
        // with a stale persisted one.
        void injectBehaviourEntityId(uint64_t instanceId);
    };
}
