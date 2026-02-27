#include "PluginContextImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ScriptingEvents.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "Pipeline.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <filesystem>

namespace plugin {

    PluginContextImpl::PluginContextImpl(const std::string& pluginName,
                                         const std::unordered_set<std::string>& capabilities)
        : pluginName(pluginName)
        , capabilities(capabilities)
    {
    }

    PluginContextImpl::~PluginContextImpl()
    {
        cleanupAll();
    }

    events::EventDispatcher& PluginContextImpl::getEventDispatcher()
    {
        return events::EventDispatcher::instance();
    }

    events::SubscriptionToken PluginContextImpl::managedSubscribe(events::SubscriptionToken token)
    {
        managedSubscriptions.push_back(token);
        return token;
    }

    void PluginContextImpl::registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window)
    {
        if (!hasCapability("editor")) {
            vfLogWarning("[Plugin:{}] Cannot register editor window - editor capability not available", pluginName);
            return;
        }

        controllers::imguiHandler::ImguiWindowHandler::add(window);
        registeredWindows.push_back(std::move(window));
    }

    void PluginContextImpl::registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage)
    {
        if (!hasCapability("import")) {
            vfLogWarning("[Plugin:{}] Cannot register import stage - import capability not available", pluginName);
            return;
        }

        // Import pipeline stage registration will be wired by PluginManager
        // For now, log that it was registered
        vfLogInfo("[Plugin:{}] Import stage registration requested (not yet wired)", pluginName);
    }

    void PluginContextImpl::registerScriptFunction(const std::string& name, std::any function)
    {
        if (!hasCapability("scripting")) {
            vfLogWarning("[Plugin:{}] Cannot register script function '{}' - scripting capability not available", pluginName, name);
            return;
        }

        events::scripting::RegisterNativeScriptFunctionCommand cmd;
        cmd.functionName = name;
        cmd.function = std::move(function);
        events::EventDispatcher::instance().execute(cmd);

        vfLogInfo("[Plugin:{}] Registered native script function: {}", pluginName, name);
    }

    bool PluginContextImpl::hasCapability(const std::string& capability) const
    {
        return capabilities.contains(capability);
    }

    ImGuiContext* PluginContextImpl::getImGuiContext()
    {
        return ImGui::GetCurrentContext();
    }

    std::string PluginContextImpl::getPluginDataPath() const
    {
        auto path = std::filesystem::current_path() / "plugins" / "data" / pluginName;
        std::filesystem::create_directories(path);
        return path.string();
    }

    void PluginContextImpl::logInfo(const std::string& message)
    {
        vfLogInfo("[Plugin:{}] {}", pluginName, message);
    }

    void PluginContextImpl::logWarning(const std::string& message)
    {
        vfLogWarning("[Plugin:{}] {}", pluginName, message);
    }

    void PluginContextImpl::logError(const std::string& message)
    {
        vfLogError("[Plugin:{}] {}", pluginName, message);
    }

    void PluginContextImpl::cleanupAll()
    {
        // Unsubscribe all managed event subscriptions
        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& token : managedSubscriptions) {
            if (token.isValid()) {
                dispatcher.unsubscribe(token);
            }
        }
        managedSubscriptions.clear();

        // Remove all registered editor windows
        for (const auto& window : registeredWindows) {
            controllers::imguiHandler::ImguiWindowHandler::remove(window);
        }
        registeredWindows.clear();
    }

}
