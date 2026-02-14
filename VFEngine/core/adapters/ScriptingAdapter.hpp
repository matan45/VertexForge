#pragma once
#include "../../services/providers/IScriptingProvider.hpp"
#include "../../services/events/EventDispatcher.hpp"
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
    class ScriptingAdapter : public ::services::IScriptingProvider
    {
    private:
        std::unique_ptr<::services::ScriptInterpreter> interpreter;
        std::unique_ptr<NativeAPIRegistry> apiRegistry;

        // Script instance tracking
        std::unordered_map<uint64_t, std::string> instanceToClassName; // instanceId -> class name
        std::unordered_map<uint64_t, ::services::EntityHandle> instanceToEntity; // instanceId -> entity
        std::unordered_map<uint64_t, std::any> instanceToObject; // instanceId -> script object instance (type-erased)
        std::unordered_map<std::string, std::string> pathToClassName; // scriptPath -> class name

        // Interface implementation cache (for collision/trigger callbacks)
        std::unordered_map<uint64_t, std::unordered_set<std::string>> instanceToInterfaces; // instanceId -> implemented interfaces

        std::unordered_map<uint64_t, ::services::ScriptPlaybackState> instanceToPlaybackState;

        // Error tracking
        mutable std::optional<::services::ScriptError> lastError;


        uint64_t nextInstanceId = 1;
        std::string scriptLibraryPath;
        bool initialized = false;
        bool compiled = false;

        // Physics collision callback subscription tokens
        ::events::SubscriptionToken collisionStartToken;
        ::events::SubscriptionToken collisionEndToken;
        ::events::SubscriptionToken triggerEnterToken;
        ::events::SubscriptionToken triggerExitToken;

        // UI Button callback subscription tokens
        ::events::SubscriptionToken buttonClickedToken;
        ::events::SubscriptionToken buttonPressedToken;
        ::events::SubscriptionToken buttonReleasedToken;
        ::events::SubscriptionToken buttonHoverEnterToken;
        ::events::SubscriptionToken buttonHoverExitToken;

        // UI TextInput callback subscription tokens
        ::events::SubscriptionToken textInputSubmitToken;
        ::events::SubscriptionToken textInputChangedToken;
        ::events::SubscriptionToken textInputFocusedToken;
        ::events::SubscriptionToken textInputUnfocusedToken;

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

    private:
        void setError(::services::ScriptError::Type type, const std::string& message,
                      const std::string& file = "", int line = 0);

        std::string extractClassName(const std::string& scriptPath);

        std::string getLibraryPath(const std::string& manifestPath) const;

        // Physics collision callback helpers
        void subscribeToPhysicsEvents();
        void unsubscribeFromPhysicsEvents();
        void dispatchCollisionCallback(const char* methodName,
                                       ::services::EntityHandle self, ::services::EntityHandle other);

        // UI Button callback helpers
        void subscribeToUIButtonEvents();
        void unsubscribeFromUIButtonEvents();
        void dispatchUIButtonCallback(const char* methodName,
                                      ::services::EntityHandle buttonEntity,
                                      const std::string& entityName);

        // UI TextInput callback helpers
        void subscribeToUITextInputEvents();
        void unsubscribeFromUITextInputEvents();
        void dispatchUITextInputCallback(const char* methodName,
                                         ::services::EntityHandle entity,
                                         const std::string& entityName,
                                         const std::string& text = "");
    };
}
