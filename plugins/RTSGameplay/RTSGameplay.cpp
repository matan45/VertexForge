#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"
#include "RTSComponents.hpp"
#include "CombatRules.hpp"
#include "FogOfWar.hpp"
#include "FogOfWarWindow.hpp"
#include <imgui.h>
#include <algorithm>
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

        ctx->registerNativeComponent<HealthComponent>("Health")
            .data<&HealthComponent::maxHP>("maxHP")
            .data<&HealthComponent::currentHP>("currentHP");

        ctx->registerNativeComponent<AttackComponent>("Attack")
            .data<&AttackComponent::damage>("damage")
            .data<&AttackComponent::range>("range")
            .data<&AttackComponent::cooldown>("cooldown")
            .data<&AttackComponent::lastAttackTime>("lastAttackTime");

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
    // float-or-int numeric argument (mType passes float literals as FLOAT but
    // an int expression is legal too).
    static float numberArg(const MTypePluginHost* host, const MTypeValue* v)
    {
        return host->getTag(v) == MT_TAG_FLOAT ? static_cast<float>(host->getFloat(v))
                                               : static_cast<float>(host->getInt(v));
    }

    // mType natives backed by plugin state, registered through mType's plugin
    // C ABI (PluginHostApi.h): capture-less MTypeNativeFn + this as userData,
    // values built/inspected via the engine-provided host vtable. The engine
    // auto-unregisters these when the plugin unloads.
    void registerScriptNatives()
    {
        if (!ctx->hasCapability(std::string(plugin::capability::scripting)))
            return;
        host = ctx->getScriptHost();
        if (!host)
            return;

        // _rts_fog_state(x, z) -> int: 0 unexplored, 1 explored, 2 visible.
        ctx->registerScriptFunction("_rts_fog_state",
            [](void* userData, MTypeContext* c, const MTypeValue* const* args, int argc) -> MTypeValue*
            {
                auto* self = static_cast<RTSGameplay*>(userData);
                const MTypePluginHost* h = self->host;
                if (argc < 2) return h->makeInt(c, 2);
                const float x = numberArg(h, args[0]);
                const float z = numberArg(h, args[1]);
                return h->makeInt(c, self->fogOfWar.queryFogState(x, z));
            }, this);

        // _rts_fog_states(float[] xs, float[] zs) -> int[]: batch form of the
        // above (one native call per query set — e.g. a footprint or an AI
        // scan). Demonstrates array args + array results through the host vtable.
        ctx->registerScriptFunction("_rts_fog_states",
            [](void* userData, MTypeContext* c, const MTypeValue* const* args, int argc) -> MTypeValue*
            {
                auto* self = static_cast<RTSGameplay*>(userData);
                const MTypePluginHost* h = self->host;
                if (argc < 2) return h->makeArray(c, MT_TAG_INT, 0);

                const size_t count = std::min(h->arrayLen(args[0]), h->arrayLen(args[1]));
                MTypeValue* result = h->makeArray(c, MT_TAG_INT, count);
                for (size_t i = 0; i < count; ++i)
                {
                    const float x = numberArg(h, h->arrayGet(c, args[0], i));
                    const float z = numberArg(h, h->arrayGet(c, args[1], i));
                    h->arraySet(result, i, h->makeInt(c, self->fogOfWar.queryFogState(x, z)));
                }
                return result;
            }, this);

        // _rts_apply_damage(targetId, amount) -> int: applies `amount` damage to
        // the target's Health (VK-1404). Returns 1 if this killed it, 0 if it
        // survived (incl. a no-op for amount <= 0), -1 if the target is invalid or
        // has no Health. On a kill it publishes "rts.unit_killed" {entity:id} — the
        // native never destroys the entity; scripts handle death/cleanup via the event.
        ctx->registerScriptFunction("_rts_apply_damage",
            [](void* userData, MTypeContext* c, const MTypeValue* const* args, int argc) -> MTypeValue*
            {
                auto* self = static_cast<RTSGameplay*>(userData);
                const MTypePluginHost* h = self->host;
                if (argc < 2) return h->makeInt(c, static_cast<int>(DamageResult::NoHealth));

                const auto targetId = static_cast<uint32_t>(h->getInt(args[0]));
                const float amount = numberArg(h, args[1]);

                auto& reg = self->ctx->getRegistry();
                const auto e = static_cast<entt::entity>(targetId);
                if (!reg.valid(e) || !reg.all_of<HealthComponent>(e))
                    return h->makeInt(c, static_cast<int>(DamageResult::NoHealth));

                const DamageResult result = applyDamageToHealth(reg.get<HealthComponent>(e), amount);
                if (result == DamageResult::Killed)
                    self->ctx->publishEvent("rts.unit_killed", {{"entity", static_cast<int>(targetId)}});

                return h->makeInt(c, static_cast<int>(result));
            }, this);
    }

    plugin::PluginContext* ctx = nullptr;
    const MTypePluginHost* host = nullptr;
    std::shared_ptr<FogSettings> fogSettings;
    FogOfWarSystem fogOfWar;
};

VF_IMPLEMENT_PLUGIN(RTSGameplay)
