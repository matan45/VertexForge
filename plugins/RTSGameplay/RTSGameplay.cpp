#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "RTSComponents.hpp"
#include "FogOfWar.hpp"
#include "FogOfWarWindow.hpp"
#include <environment/registry/NativeDelegate.hpp>
#include <imgui.h>
#include <any>
#include <memory>
#include <span>
#include <string>

// RTSGameplay plugin entry point: registers the RTS tag components (VK-1302)
// and runs the fog-of-war system (VK-1314). See RTSComponents.hpp, FogOfWar.*
// and FogOfWarWindow.hpp for the pieces.

class RTSGameplay : public plugin::IPlugin
{
public:
    plugin::PluginInfo getInfo() const override
    {
        return {"RTSGameplay", "VertexForge",
                "RTS gameplay: Selectable/Selected/Team/Vision components + fog of war (VK-1302/VK-1314)", 1, 1, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;

        ctx->registerNativeComponent<SelectableComponent>("Selectable")
            .data<&SelectableComponent::canBeSelected>("canBeSelected");

        ctx->registerNativeComponent<SelectedComponent>("Selected")
            .data<&SelectedComponent::active>("active");

        ctx->registerNativeComponent<TeamComponent>("Team")
            .data<&TeamComponent::teamId>("teamId");

        ctx->registerNativeComponent<VisionComponent>("Vision")
            .data<&VisionComponent::sightRadius>("sightRadius");

        fogSettings = std::make_shared<FogSettings>();
        fogOfWar.initialize(ctx, fogSettings);

        registerScriptNatives();

        if (ctx->hasCapability(std::string(plugin::capability::editor)))
        {
            ImGui::SetCurrentContext(ctx->getImGuiContext());
            ctx->registerEditorWindow(std::make_shared<FogOfWarWindow>(fogSettings), "Fog of War");
        }

        ctx->logInfo("RTSGameplay initialized - Selectable/Selected/Team/Vision components registered");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        (void)deltaTime;
        fogOfWar.update();
    }

    void onShutdown() override
    {
        fogOfWar.shutdown();
        ctx->logInfo("RTSGameplay shutdown");
    }

private:
    // mType natives backed by plugin state. NativeDelegate is a {userData, fn
    // pointer} pair — the capture-less lambda gets the fog system via userData.
    // Primitive Value use only (header-inline, no mType.lib in the plugin);
    // the engine auto-unregisters these when the plugin unloads.
    void registerScriptNatives()
    {
        if (!ctx->hasCapability(std::string(plugin::capability::scripting)))
            return;

        // _rts_fog_state(x, z) -> int: 0 unexplored, 1 explored, 2 visible.
        environment::registry::NativeDelegate fogState;
        fogState.userData = &fogOfWar;
        fogState.invoke = [](void* userData, environment::NativeContext&,
                             std::span<const value::Value> args) -> value::Value
        {
            if (args.size() < 2) return value::Value(2);
            const auto* fog = static_cast<const FogOfWarSystem*>(userData);
            const float x = value::isFloat(args[0]) ? static_cast<float>(value::asFloat(args[0]))
                                                    : static_cast<float>(value::asInt(args[0]));
            const float z = value::isFloat(args[1]) ? static_cast<float>(value::asFloat(args[1]))
                                                    : static_cast<float>(value::asInt(args[1]));
            return value::Value(fog->queryFogState(x, z));
        };
        ctx->registerScriptFunction("_rts_fog_state", std::any(fogState));
    }

    plugin::PluginContext* ctx = nullptr;
    std::shared_ptr<FogSettings> fogSettings;
    FogOfWarSystem fogOfWar;
};

VF_IMPLEMENT_PLUGIN(RTSGameplay)
