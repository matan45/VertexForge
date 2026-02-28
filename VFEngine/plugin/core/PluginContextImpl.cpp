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
        if (!hasCapability(std::string(capability::editor))) {
            vfLogWarning("[Plugin:{}] Cannot register editor window - editor capability not available", pluginName);
            return;
        }

        controllers::imguiHandler::ImguiWindowHandler::add(window);
        registeredWindows.push_back(std::move(window));
    }

    void PluginContextImpl::registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage)
    {
        if (!hasCapability(std::string(capability::import_))) {
            vfLogWarning("[Plugin:{}] Cannot register import stage - import capability not available", pluginName);
            return;
        }

        if (!stage) {
            vfLogWarning("[Plugin:{}] Cannot register null import stage", pluginName);
            return;
        }

        vfLogInfo("[Plugin:{}] Registered import stage: {}", pluginName, stage->getName());
        registeredImportStages.push_back(std::move(stage));
    }

    std::vector<std::unique_ptr<pipeline::PipelineStage>> PluginContextImpl::takeImportStages()
    {
        return std::move(registeredImportStages);
    }

    void PluginContextImpl::registerScriptFunction(const std::string& name, std::any function)
    {
        if (!hasCapability(std::string(capability::scripting))) {
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
        // Sanitize pluginName: strip path separators and traversal sequences
        std::string safeName;
        safeName.reserve(pluginName.size());
        for (char c : pluginName) {
            if (c == '/' || c == '\\' || c == '\0') {
                safeName += '_';
            } else {
                safeName += c;
            }
        }
        // Reject names that are entirely dots (e.g. "..", "...")
        if (safeName.find_first_not_of('.') == std::string::npos) {
            safeName = "_plugin_";
        }

        auto path = std::filesystem::current_path() / "plugins" / "data" / safeName;
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
        auto& dispatcher = events::EventDispatcher::instance();
        for (const auto& token : managedSubscriptions) {
            if (token.isValid()) {
                dispatcher.unsubscribe(token);
            }
        }
        managedSubscriptions.clear();

        for (const auto& window : registeredWindows) {
            controllers::imguiHandler::ImguiWindowHandler::remove(window);
        }
        registeredWindows.clear();
    }

}
