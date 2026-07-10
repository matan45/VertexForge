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

        using plugin::inspector::Field;

        ctx->registerNativeComponent<SelectableComponent>("Selectable")
            .data<&SelectableComponent::canBeSelected>("canBeSelected")
            .data<&SelectableComponent::portrait>("portrait");
        ctx->setFieldAttributes("Selectable", "canBeSelected",
            Field{}.name("Can Be Selected").help("Whether the RTS selection system can pick this unit").build());
        ctx->setFieldAttributes("Selectable", "portrait",
            Field{}.name("Portrait").help("Icon shown in the selection panel — drag a .vfImage here").asset(".vfimage").build());

        ctx->registerNativeComponent<SelectedComponent>("Selected")
            .data<&SelectedComponent::active>("active");

        ctx->registerNativeComponent<TeamComponent>("Team")
            .data<&TeamComponent::teamId>("teamId")
            .data<&TeamComponent::color>("color");
        ctx->setFieldAttributes("Team", "teamId",
            Field{}.name("Team").help("0 = Player, 1 = Enemy, 2 = Neutral").range(0.f, 2.f, 1.f).slider().group("Faction").build());
        ctx->setFieldAttributes("Team", "color",
            Field{}.name("Team Color").help("Tint used for minimap blips and unit highlight").color().group("Faction").build());

        ctx->registerNativeComponent<VisionComponent>("Vision")
            .data<&VisionComponent::sightRadius>("sightRadius");
        ctx->setFieldAttributes("Vision", "sightRadius",
            Field{}.name("Sight Radius").help("Fog-of-war reveal radius around this unit").range(0.f, 100.f, 0.5f).slider().units("m").build());

        ctx->registerNativeComponent<HealthComponent>("Health")
            .data<&HealthComponent::maxHP>("maxHP")
            .data<&HealthComponent::currentHP>("currentHP");
        ctx->setFieldAttributes("Health", "maxHP",
            Field{}.name("Max HP").help("Maximum hit points").range(1.f, 2000.f, 1.f).slider().units("hp").group("Vitals").build());
        ctx->setFieldAttributes("Health", "currentHP",
            Field{}.name("Current HP").help("Current hit points (clamped to Max HP at runtime)").range(0.f, 2000.f, 1.f).slider().units("hp").group("Vitals").build());

        ctx->registerNativeComponent<AttackComponent>("Attack")
            .data<&AttackComponent::damage>("damage")
            .data<&AttackComponent::range>("range")
            .data<&AttackComponent::cooldown>("cooldown")
            .data<&AttackComponent::lastAttackTime>("lastAttackTime");
        ctx->setFieldAttributes("Attack", "damage",
            Field{}.name("Damage").help("Damage applied per hit").range(0.f, 500.f, 1.f).slider().group("Combat").build());
        ctx->setFieldAttributes("Attack", "range",
            Field{}.name("Range").help("Engagement range in world units").range(0.f, 50.f, 0.5f).slider().units("m").group("Combat").build());
        ctx->setFieldAttributes("Attack", "cooldown",
            Field{}.name("Cooldown").help("Seconds between attacks").range(0.f, 10.f, 0.05f).slider().units("s").group("Combat").build());
        // Scratch state stamped by the combat script — hide it from designers.
        ctx->setFieldAttributes("Attack", "lastAttackTime",
            Field{}.name("Last Attack Time").hidden().build());

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

        // _rts_fog_overlayKey() -> string: the generic UI external-texture key for the
        // RGBA minimap fog overlay (VK-1488). Empty until the overlay texture is created
        // and registered; the minimap script binds it with UI::setImageExternalTexture.
        ctx->registerScriptFunction("_rts_fog_overlayKey",
            [](void* userData, MTypeContext* c, const MTypeValue* const* /*args*/, int /*argc*/) -> MTypeValue*
            {
                auto* self = static_cast<RTSGameplay*>(userData);
                const MTypePluginHost* h = self->host;
                const std::string& key = self->fogOfWar.overlayKey();
                return h->makeString(c, key.c_str(), key.size());
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
