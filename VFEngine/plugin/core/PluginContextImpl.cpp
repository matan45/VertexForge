#include "print/Log.hpp"
#include "PluginContextImpl.hpp"
#include "PluginComponentRegistry.hpp"
#include "ComponentBuilderImpl.hpp"
#include "PluginEventBus.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/render/RenderHookEvents.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/PluginComponents.hpp"
#include "Pipeline.hpp"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

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

    ComponentBuilder& PluginContextImpl::registerComponent(const std::string& componentName)
    {
        activeBuilder = std::make_unique<ComponentBuilderImpl>(this, componentName);
        return *activeBuilder;
    }

    void PluginContextImpl::finalizeComponentRegistration(ComponentBuilderImpl& builder)
    {
        std::string qualifiedName = pluginName + "::" + builder.getComponentName();

        PluginComponentInfo info;
        info.pluginName = pluginName;
        info.componentName = builder.getComponentName();
        info.qualifiedName = qualifiedName;
        info.properties = builder.getProperties();
        info.inspector = builder.getInspector();

        // Build default data from property descriptors
        info.defaultData = nlohmann::json::object();
        for (const auto& prop : info.properties)
        {
            info.defaultData[prop.name] = prop.defaultValue;
        }

        PluginComponentRegistry::instance().registerComponent(info);
        registeredComponentNames.push_back(qualifiedName);

        vfLogInfo("[Plugin:{}] Registered component: {} ({} properties)",
                  pluginName, builder.getComponentName(), info.properties.size());
    }

    bool PluginContextImpl::addPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity))
            return false;

        const auto* info = PluginComponentRegistry::instance().findComponent(qualifiedName);
        if (!info)
        {
            vfLogWarning("[Plugin:{}] Component '{}' not registered", pluginName, componentName);
            return false;
        }

        auto& pluginComp = reg.get_or_emplace<components::PluginComponentsComponent>(entity);
        if (pluginComp.components.contains(qualifiedName))
            return false;

        pluginComp.components[qualifiedName] = info->defaultData;
        return true;
    }

    bool PluginContextImpl::removePluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return false;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        bool erased = pluginComp.components.erase(qualifiedName) > 0;

        if (pluginComp.components.empty())
            reg.remove<components::PluginComponentsComponent>(entity);

        componentDataWrappers.erase(qualifiedName);
        return erased;
    }

    PluginComponentData* PluginContextImpl::getPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return nullptr;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        auto it = pluginComp.components.find(qualifiedName);
        if (it == pluginComp.components.end())
            return nullptr;

        componentDataWrappers.insert_or_assign(qualifiedName, PluginComponentData(&it->second));
        return &componentDataWrappers.at(qualifiedName);
    }

    bool PluginContextImpl::hasPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return false;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        return pluginComp.components.contains(qualifiedName);
    }

    void PluginContextImpl::forEachWithComponent(const std::string& componentName,
                                                  const std::function<void(entt::entity, PluginComponentData&)>& callback)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();
        auto view = reg.view<components::PluginComponentsComponent>();

        for (auto entity : view)
        {
            auto& pluginComp = view.get<components::PluginComponentsComponent>(entity);
            auto it = pluginComp.components.find(qualifiedName);
            if (it != pluginComp.components.end())
            {
                PluginComponentData data(&it->second);
                callback(entity, data);
            }
        }
    }

    void PluginContextImpl::publishEvent(const std::string& eventName, const nlohmann::json& data)
    {
        PluginEventBus::instance().publish(eventName, data);
    }

    events::SubscriptionToken PluginContextImpl::subscribeEvent(const std::string& eventName,
                                                                 std::function<void(const nlohmann::json&)> handler)
    {
        auto token = PluginEventBus::instance().subscribe(eventName, std::move(handler));
        pluginEventSubscriptions.push_back(token);
        return token;
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

    plugin::RenderHookHandle PluginContextImpl::registerRenderPassHook(
        plugin::RenderPassHookPoint hookPoint,
        plugin::RenderHookCallback callback)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot register render hook - graphics capability not available", pluginName);
            return {};
        }

        events::renderhook::RegisterRenderPassHookCommand cmd;
        cmd.hookPoint = hookPoint;
        cmd.callback = std::move(callback);
        auto handle = events::EventDispatcher::instance().execute(cmd);

        if (handle.isValid()) {
            registeredRenderHooks.push_back(handle);
            vfLogInfo("[Plugin:{}] Registered render pass hook at point {}", pluginName, static_cast<uint32_t>(hookPoint));
        }

        return handle;
    }

    void PluginContextImpl::unregisterRenderPassHook(plugin::RenderHookHandle handle)
    {
        if (!handle.isValid()) return;

        events::renderhook::UnregisterRenderPassHookCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(registeredRenderHooks,
            [&](const plugin::RenderHookHandle& h) { return h.id == handle.id; });
    }

    entt::registry& PluginContextImpl::getRegistry()
    {
        return scene::EntityRegistry::getRegistry();
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
        std::string safeName;
        safeName.reserve(pluginName.size());
        for (char c : pluginName) {
            if (c == '/' || c == '\\' || c == '\0') {
                safeName += '_';
            } else {
                safeName += c;
            }
        }
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

        for (const auto& handle : registeredRenderHooks) {
            events::renderhook::UnregisterRenderPassHookCommand cmd;
            cmd.handle = handle;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        registeredRenderHooks.clear();

        for (const auto& window : registeredWindows) {
            controllers::imguiHandler::ImguiWindowHandler::remove(window);
        }
        registeredWindows.clear();

        for (const auto& token : pluginEventSubscriptions) {
            PluginEventBus::instance().unsubscribe(token);
        }
        pluginEventSubscriptions.clear();

        // Clean up plugin components from all entities
        if (!registeredComponentNames.empty())
        {
            auto& reg = scene::EntityRegistry::getRegistry();
            auto view = reg.view<components::PluginComponentsComponent>();

            // Collect entities to remove component from (can't modify during iteration)
            std::vector<entt::entity> toRemove;

            for (auto entity : view)
            {
                auto& pluginComp = view.get<components::PluginComponentsComponent>(entity);
                for (const auto& name : registeredComponentNames)
                {
                    pluginComp.components.erase(name);
                }
                if (pluginComp.components.empty())
                {
                    toRemove.push_back(entity);
                }
            }

            for (auto entity : toRemove)
            {
                reg.remove<components::PluginComponentsComponent>(entity);
            }

            PluginComponentRegistry::instance().unregisterPlugin(pluginName);
            registeredComponentNames.clear();
        }

        componentDataWrappers.clear();
    }

}
