#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "RTSComponents.hpp"
#include "FogOfWar.hpp"
#include "FogOfWarWindow.hpp"
#include <imgui.h>
#include <memory>
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
    plugin::PluginContext* ctx = nullptr;
    std::shared_ptr<FogSettings> fogSettings;
    FogOfWarSystem fogOfWar;
};

VF_IMPLEMENT_PLUGIN(RTSGameplay)
