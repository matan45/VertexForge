#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations
struct ImGuiContext;

namespace events {
    class EventDispatcher;
    struct SubscriptionToken;
}

namespace controllers::imguiHandler {
    class ImguiWindow;
}

namespace pipeline {
    class PipelineStage;
}

namespace plugin {

    class PluginContext
    {
    public:
        virtual ~PluginContext() = default;

        // === Event System Access ===

        // Returns the global EventDispatcher singleton.
        virtual events::EventDispatcher& getEventDispatcher() = 0;

        // Track a subscription for automatic cleanup when the plugin is unloaded.
        // Returns the same token passed in.
        virtual events::SubscriptionToken managedSubscribe(events::SubscriptionToken token) = 0;

        // === Editor Window Registration ===
        // Only available when hasCapability("editor") is true.
        virtual void registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window) = 0;

        // === Import Pipeline Extension ===
        // Only available when hasCapability("import") is true.
        virtual void registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage) = 0;

        // === Capability Queries ===
        // Check if an engine capability is available. Examples: "editor", "audio", "physics", "import"
        virtual bool hasCapability(const std::string& capability) const = 0;

        // === ImGui Context ===
        // Plugins must call ImGui::SetCurrentContext() with this in onInitialize.
        virtual ImGuiContext* getImGuiContext() = 0;

        // === Plugin Data Directory ===
        // Returns a persistent directory path for this plugin's data storage.
        virtual std::string getPluginDataPath() const = 0;

        // === Logging ===
        virtual void logInfo(const std::string& message) = 0;
        virtual void logWarning(const std::string& message) = 0;
        virtual void logError(const std::string& message) = 0;
    };

}
