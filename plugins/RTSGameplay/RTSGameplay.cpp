#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include <entt/entt.hpp>

// RTS gameplay tag components (VK-1302). Registered as plugin components so the
// engine core stays game-agnostic — scripts access them via PluginComponent.mt
// (has/getInt/add/remove/findAll) and designers via the Add Component popup.

// Marks an entity as selectable by the RTS selection system.
struct SelectableComponent
{
    bool canBeSelected = true;
};

// Runtime-only selection marker managed by the selection controller during play.
// Never authored in scenes (scenes are saved in edit mode, where it is absent).
struct SelectedComponent
{
    bool active = true;
};

// Faction ownership: 0 = Player, 1 = Enemy, 2 = Neutral.
struct TeamComponent
{
    int teamId = 0;
};

class RTSGameplay : public plugin::IPlugin
{
public:
    plugin::PluginInfo getInfo() const override
    {
        return {"RTSGameplay", "VertexForge", "RTS gameplay tag components: Selectable, Selected, Team (VK-1302)", 1, 0, 0};
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

        ctx->logInfo("RTSGameplay initialized - Selectable/Selected/Team components registered");
        return true;
    }

    void onShutdown() override
    {
        ctx->logInfo("RTSGameplay shutdown");
    }

private:
    plugin::PluginContext* ctx = nullptr;
};

VF_IMPLEMENT_PLUGIN(RTSGameplay)
