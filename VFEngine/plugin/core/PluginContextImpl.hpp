#pragma once
#include "../api/PluginContext.hpp"
#include "events/EventTypes.hpp"
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>

namespace controllers::imguiHandler {
    class ImguiWindow;
}

namespace pipeline {
    class PipelineStage;
}

namespace plugin {

    class PluginContextImpl : public PluginContext
    {
    private:
        std::string pluginName;
        std::unordered_set<std::string> capabilities;
        std::vector<events::SubscriptionToken> managedSubscriptions;
        std::vector<std::shared_ptr<controllers::imguiHandler::ImguiWindow>> registeredWindows;
        std::vector<std::unique_ptr<pipeline::PipelineStage>> registeredImportStages;
        std::vector<plugin::RenderHookHandle> registeredRenderHooks;

    public:
        explicit PluginContextImpl(const std::string& pluginName,
                          const std::unordered_set<std::string>& capabilities);
        ~PluginContextImpl() override;

        events::EventDispatcher& getEventDispatcher() override;
        events::SubscriptionToken managedSubscribe(events::SubscriptionToken token) override;
        void registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window) override;
        void registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage) override;
        void registerScriptFunction(const std::string& name, std::any function) override;
        plugin::RenderHookHandle registerRenderPassHook(
            plugin::RenderPassHookPoint hookPoint,
            plugin::RenderHookCallback callback) override;
        void unregisterRenderPassHook(plugin::RenderHookHandle handle) override;
        bool hasCapability(const std::string& capability) const override;
        ImGuiContext* getImGuiContext() override;
        std::string getPluginDataPath() const override;
        void logInfo(const std::string& message) override;
        void logWarning(const std::string& message) override;
        void logError(const std::string& message) override;

        // Called by PluginManager during shutdown to clean up all registrations.
        void cleanupAll();

        // Returns and releases ownership of all registered import stages.
        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeImportStages();

   
    };

}
