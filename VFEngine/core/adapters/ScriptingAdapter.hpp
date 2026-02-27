#pragma once
#include "../../services/providers/IScriptingProvider.hpp"
#include "NativeAPIRegistry.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptUIEventBridge;
    class ScriptPhysicsEventBridge;
    class ScriptAnimationEventBridge;
    class ScriptSocketEventBridge;
    class ScriptVFXEventBridge;

    class ScriptingAdapter : public ::services::IScriptingProvider
    {
    private:
        std::unique_ptr<::services::ScriptInterpreter> interpreter;
        std::unique_ptr<NativeAPIRegistry> apiRegistry;
        std::unique_ptr<ScriptUIEventBridge> uiEventBridge;
        std::unique_ptr<ScriptPhysicsEventBridge> physicsEventBridge;
        std::unique_ptr<ScriptAnimationEventBridge> animationEventBridge;
        std::unique_ptr<ScriptSocketEventBridge> socketEventBridge;
        std::unique_ptr<ScriptVFXEventBridge> vfxEventBridge;

        std::unordered_map<uint64_t, std::string> instanceToClassName;
        std::unordered_map<uint64_t, ::services::EntityHandle> instanceToEntity;
        std::unordered_map<uint64_t, std::any> instanceToObject;
        std::unordered_map<std::string, std::string> pathToClassName;
        std::unordered_map<uint64_t, std::unordered_set<std::string>> instanceToInterfaces;
        std::unordered_map<uint64_t, ::services::ScriptPlaybackState> instanceToPlaybackState;

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
        void callOnDestroy(uint64_t instanceId) override;

        void playVFX(uint64_t instanceId) override;

        std::optional<::services::ScriptError> getLastError() const override;
        void clearError() override;

        void setScriptLibraryPath(const std::string& path) override;

        void registerPluginNativeFunction(const std::string& name, std::any function) override;

    private:
        void setError(::services::ScriptError::Type type, const std::string& message,
                      const std::string& file = "", int line = 0);

        std::string extractClassName(const std::string& scriptPath);
        std::string getLibraryPath(const std::string& manifestPath) const;
    };
}
