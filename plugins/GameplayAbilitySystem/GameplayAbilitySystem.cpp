#include "api/IPlugin.hpp"
#include "api/PluginExport.hpp"
#include "api/PluginContext.hpp"

#include "gas/components/GASComponents.hpp"
#include "gas/runtime/GASRuntime.hpp"
#include "gas/core/GASAssets.hpp"
#include "gas/editor/AbilityEditorWindow.hpp"
#include "gas/editor/EffectEditorWindow.hpp"
#include "gas/editor/TagTableEditorWindow.hpp"
#include "gas/editor/GASDebuggerWindow.hpp"

#include <imgui.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

// Gameplay Ability System plugin entry point (VK-816). Registers the four GAS_
// components, the four data-driven asset types (via the VK-1449 plugin
// asset-type SDK), the _gas_* mType natives (facade: GameplayAbilitySystem.mt),
// and drives the runtime each frame. All gameplay logic lives in gas/core
// (engine-free, unit-tested) + GASRuntime; this file is the engine wiring.

namespace
{
    // --- mType argument helpers (the host vtable speaks plain C; copy out). ---
    std::string strArg(const MTypePluginHost* h, const MTypeValue* v)
    {
        size_t len = 0;
        const char* s = h->getString(v, &len);
        return s ? std::string(s, len) : std::string();
    }

    std::vector<std::string> strArrayArg(const MTypePluginHost* h, MTypeContext* c, const MTypeValue* v)
    {
        std::vector<std::string> out;
        const size_t n = h->arrayLen(v);
        out.reserve(n);
        for (size_t i = 0; i < n; ++i)
            out.push_back(strArg(h, h->arrayGet(c, v, i)));
        return out;
    }

    std::uint32_t entityArg(const MTypePluginHost* h, const MTypeValue* v)
    {
        return static_cast<std::uint32_t>(h->getInt(v));
    }
}

class GameplayAbilitySystem : public plugin::IPlugin
{
public:
    plugin::PluginInfo getInfo() const override
    {
        return {"GameplayAbilitySystem", "VertexForge",
                "Gameplay Ability System: abilities, attributes, effects, tags, cooldowns and cues (VK-816)",
                1, 0, 0};
    }

    bool onInitialize(plugin::PluginContext* context) override
    {
        ctx = context;
        registerComponents();
        registerAssetTypes();

        runtime.init(
            ctx,
            [this](const gas::CueEvent& ev) { realizeCue(ev); },
            [this](const std::string& name, const nlohmann::json& data) { ctx->publishEvent(name, data); });

        registerScriptNatives();
        registerEditorWindows();
        registerDebuggerEvents();

        // VK-1449 open-routing: double-clicking a GAS asset publishes
        // "vf.asset.open"; route the gas.* type ids to the matching window.
        ctx->managedSubscribe(ctx->subscribeEvent("vf.asset.open",
            [this](const nlohmann::json& data)
            {
                const std::string typeId = data.value("typeId", std::string());
                const std::string path = data.value("path", std::string());
                if (typeId == "gas.ability" && abilityWindow) abilityWindow->open(path);
                else if (typeId == "gas.effect" && effectWindow) effectWindow->open(path);
                else if (typeId == "gas.tags" && tagWindow) tagWindow->open(path);
            }));

        ctx->logInfo("GameplayAbilitySystem initialized (GAS_* components + .vfAbility/.vfGameplayEffect/"
                     ".vfGameplayCue/.vfGameplayTags + _gas_* natives + editor windows)");
        return true;
    }

    void onUpdate(float deltaTime) override
    {
        // deltaTime is the scaled GAME delta (0 in edit mode / when paused after
        // the editor Plugins-tick fix), so timed effects pause outside play and
        // input is only polled while the game is actually running.
        runtime.setGameActive(deltaTime > 0.0f);
        runtime.tick(deltaTime);
    }

    void onDeactivate() override { runtime.setGameActive(false); }

    void onShutdown() override
    {
        runtime.reset();
        if (ctx) ctx->logInfo("GameplayAbilitySystem shutdown");
    }

private:
    // ----------------------------------------------------------- components
    void registerComponents()
    {
        using plugin::inspector::Field;

        ctx->registerNativeComponent<GAS_AbilitySystemComponent>("GAS_AbilitySystem")
            .data<&GAS_AbilitySystemComponent::grantedAbilities>("grantedAbilities")
            .data<&GAS_AbilitySystemComponent::pollInput>("pollInput");
        ctx->setFieldAttributes("GAS_AbilitySystem", "grantedAbilities",
            Field{}.name("Granted Abilities").help("Abilities this entity can activate — drag .vfAbility assets")
                   .asset(".vfability").group("Abilities").build());
        ctx->setFieldAttributes("GAS_AbilitySystem", "pollInput",
            Field{}.name("Poll Input").help("Auto-activate granted OnPressed abilities from their bound input action")
                   .group("Abilities").build());

        ctx->registerNativeComponent<GAS_AttributeSetComponent>("GAS_AttributeSet")
            .data<&GAS_AttributeSetComponent::attributeNames>("attributeNames")
            .data<&GAS_AttributeSetComponent::baseValues>("baseValues")
            .data<&GAS_AttributeSetComponent::minValues>("minValues")
            .data<&GAS_AttributeSetComponent::maxValues>("maxValues")
            .data<&GAS_AttributeSetComponent::currentValues>("currentValues");
        ctx->setFieldAttributes("GAS_AttributeSet", "attributeNames",
            Field{}.name("Attribute Names").help("Named attributes (e.g. Health, Mana). Parallel to the value arrays.").group("Attributes").build());
        ctx->setFieldAttributes("GAS_AttributeSet", "baseValues",
            Field{}.name("Base Values").help("Base value per attribute").group("Attributes").build());
        ctx->setFieldAttributes("GAS_AttributeSet", "minValues",
            Field{}.name("Min Values").help("Clamp minimum per attribute").group("Attributes").build());
        ctx->setFieldAttributes("GAS_AttributeSet", "maxValues",
            Field{}.name("Max Values").help("Clamp maximum per attribute").group("Attributes").build());
        ctx->setFieldAttributes("GAS_AttributeSet", "currentValues",
            Field{}.name("Current (runtime)").help("Aggregated current values — runtime mirror, read-only").readOnly().group("Attributes").build());

        ctx->registerNativeComponent<GAS_TagComponent>("GAS_Tags")
            .data<&GAS_TagComponent::looseTags>("looseTags")
            .data<&GAS_TagComponent::grantedTags>("grantedTags");
        ctx->setFieldAttributes("GAS_Tags", "looseTags",
            Field{}.name("Loose Tags").help("Authored gameplay tags (dotted, e.g. Character.Hero)").group("Tags").build());
        ctx->setFieldAttributes("GAS_Tags", "grantedTags",
            Field{}.name("Granted (runtime)").help("Effect/ability-granted tags — runtime mirror, read-only").readOnly().group("Tags").build());

        ctx->registerNativeComponent<GAS_ActiveEffectsComponent>("GAS_ActiveEffects")
            .data<&GAS_ActiveEffectsComponent::startupEffects>("startupEffects")
            .data<&GAS_ActiveEffectsComponent::activeEffectSummary>("activeEffectSummary");
        ctx->setFieldAttributes("GAS_ActiveEffects", "startupEffects",
            Field{}.name("Startup Effects").help("Effects applied when the entity is first seeded — drag .vfGameplayEffect assets")
                   .asset(".vfgameplayeffect").group("Effects").build());
        ctx->setFieldAttributes("GAS_ActiveEffects", "activeEffectSummary",
            Field{}.name("Active (runtime)").help("Currently-active effects — runtime mirror, read-only").readOnly().group("Effects").build());
    }

    // ----------------------------------------------------------- asset types
    void registerAssetTypes()
    {
        {
            plugin::PluginAssetTypeDesc d;
            d.typeId = "gas.ability";
            d.displayName = "Gameplay Ability";
            d.category = "Gameplay Ability System";
            d.extensions = {".vfability"};
            d.jsonContainer = true;
            d.createMenuEntry = true;
            d.icon = "ability";
            d.badgeColor = 0xFF4F9EF7;
            d.defaultTemplate = gas::AbilityAsset::toJson(gas::AbilityAsset::createDefault("New Ability")).dump(4);
            ctx->registerAssetType(d);
        }
        {
            plugin::PluginAssetTypeDesc d;
            d.typeId = "gas.effect";
            d.displayName = "Gameplay Effect";
            d.category = "Gameplay Ability System";
            d.extensions = {".vfgameplayeffect"};
            d.jsonContainer = true;
            d.createMenuEntry = true;
            d.icon = "effect";
            d.badgeColor = 0xFF4FF7DD;
            d.defaultTemplate = gas::EffectAsset::toJson(gas::EffectAsset::createDefault("New Effect")).dump(4);
            ctx->registerAssetType(d);
        }
        {
            plugin::PluginAssetTypeDesc d;
            d.typeId = "gas.cue";
            d.displayName = "Gameplay Cue";
            d.category = "Gameplay Ability System";
            d.extensions = {".vfgameplaycue"};
            d.jsonContainer = true;
            d.createMenuEntry = true;
            d.icon = "cue";
            d.badgeColor = 0xFFF74F9E;
            d.defaultTemplate = gas::CueAsset::toJson(gas::CueAsset::createDefault("New Cue")).dump(4);
            ctx->registerAssetType(d);
        }
        {
            plugin::PluginAssetTypeDesc d;
            d.typeId = "gas.tags";
            d.displayName = "Gameplay Tag Table";
            d.category = "Gameplay Ability System";
            d.extensions = {".vfgameplaytags"};
            d.jsonContainer = false;                       // a flat tag dictionary, no asset refs
            d.createMenuEntry = true;
            d.icon = "tags";
            d.badgeColor = 0xFF8AF74F;
            // Project tag tables are merged at load and may not be referenced by
            // any scene, so always pack them into exported games.
            d.alwaysIncludeGlobs = {"*.vfgameplaytags"};
            d.defaultTemplate = gas::TagsAsset::toJson(gas::TagsAsset::createDefault("Project Tags")).dump(4);
            ctx->registerAssetType(d);
        }
    }

    // ----------------------------------------------------------- cue realize
    void realizeCue(const gas::CueEvent& ev)
    {
        if (!ev.vfxPath.empty() && ctx->hasCapability(std::string(plugin::capability::vfx)))
        {
            services::VFXRuntimeParams p;
            p.vfxAssetPath = ev.vfxPath;
            p.loop = false;
            p.autoDestroy = true;       // fire-and-forget
            glm::mat4 m(1.0f);
            m[3][0] = ev.x;
            m[3][1] = ev.y;
            m[3][2] = ev.z;
            p.worldTransform = m;
            const services::VFXInstanceId id = ctx->createVFXInstance(p);
            ctx->playVFXInstance(id);
        }
        if (!ev.audioPath.empty() && ctx->hasCapability(std::string(plugin::capability::audio)))
        {
            services::AudioParams ap;
            ctx->playSound3D(ev.audioPath, glm::vec3(ev.x, ev.y, ev.z), ap);
        }
    }

    // -------------------------------------------------------------- natives
    void registerScriptNatives()
    {
        if (!ctx->hasCapability(std::string(plugin::capability::scripting)))
            return;
        host = ctx->getScriptHost();
        if (!host)
            return;

        ctx->registerScriptFunction("_gas_grant_ability",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeInt(c, 0);
                return h->makeInt(c, self->runtime.grantAbility(entityArg(h, a[0]), strArg(h, a[1])));
            }, this);

        ctx->registerScriptFunction("_gas_revoke_ability",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeInt(c, 0);
                return h->makeInt(c, self->runtime.revokeAbility(entityArg(h, a[0]), strArg(h, a[1])));
            }, this);

        ctx->registerScriptFunction("_gas_can_activate",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 3) return h->makeInt(c, static_cast<int>(gas::ActivationStatus::InvalidEntity));
                const auto s = self->runtime.canActivate(entityArg(h, a[0]), strArg(h, a[1]), h->getInt(a[2]));
                return h->makeInt(c, static_cast<int>(s));
            }, this);

        ctx->registerScriptFunction("_gas_activate",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 3) return h->makeInt(c, static_cast<int>(gas::ActivationStatus::InvalidEntity));
                const auto s = self->runtime.activate(entityArg(h, a[0]), strArg(h, a[1]), h->getInt(a[2]));
                return h->makeInt(c, static_cast<int>(s));
            }, this);

        ctx->registerScriptFunction("_gas_cancel",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n >= 2) self->runtime.cancelAbility(entityArg(h, a[0]), static_cast<std::uint64_t>(h->getInt(a[1])));
                return h->makeVoid(c);
            }, this);

        ctx->registerScriptFunction("_gas_apply_effect",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 3) return h->makeInt(c, 0);
                const auto handle = self->runtime.applyEffect(entityArg(h, a[0]), entityArg(h, a[1]), strArg(h, a[2]));
                return h->makeInt(c, static_cast<int64_t>(handle));
            }, this);

        ctx->registerScriptFunction("_gas_remove_effect",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeBool(c, 0);
                return h->makeBool(c, self->runtime.removeEffect(entityArg(h, a[0]), static_cast<std::uint64_t>(h->getInt(a[1]))) ? 1 : 0);
            }, this);

        ctx->registerScriptFunction("_gas_get_attribute",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeFloat(c, 0.0);
                return h->makeFloat(c, self->runtime.getAttribute(entityArg(h, a[0]), strArg(h, a[1])));
            }, this);

        ctx->registerScriptFunction("_gas_get_attribute_base",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeFloat(c, 0.0);
                return h->makeFloat(c, self->runtime.getAttributeBase(entityArg(h, a[0]), strArg(h, a[1])));
            }, this);

        ctx->registerScriptFunction("_gas_has_tag",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeBool(c, 0);
                return h->makeBool(c, self->runtime.hasTag(entityArg(h, a[0]), strArg(h, a[1])) ? 1 : 0);
            }, this);

        ctx->registerScriptFunction("_gas_has_all_tags",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeBool(c, 1);
                return h->makeBool(c, self->runtime.hasAllTags(entityArg(h, a[0]), strArrayArg(h, c, a[1])) ? 1 : 0);
            }, this);

        ctx->registerScriptFunction("_gas_has_any_tags",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeBool(c, 0);
                return h->makeBool(c, self->runtime.hasAnyTags(entityArg(h, a[0]), strArrayArg(h, c, a[1])) ? 1 : 0);
            }, this);

        ctx->registerScriptFunction("_gas_get_tags",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 1) return h->makeArray(c, MT_TAG_STRING, 0);
                const std::vector<std::string> tags = self->runtime.getTags(entityArg(h, a[0]));
                MTypeValue* arr = h->makeArray(c, MT_TAG_STRING, tags.size());
                for (size_t i = 0; i < tags.size(); ++i)
                    h->arraySet(arr, i, h->makeString(c, tags[i].c_str(), tags[i].size()));
                return arr;
            }, this);

        ctx->registerScriptFunction("_gas_get_cooldown_remaining",
            [](void* u, MTypeContext* c, const MTypeValue* const* a, int n) -> MTypeValue*
            {
                auto* self = static_cast<GameplayAbilitySystem*>(u);
                const MTypePluginHost* h = self->host;
                if (n < 2) return h->makeFloat(c, 0.0);
                return h->makeFloat(c, self->runtime.getCooldownRemaining(entityArg(h, a[0]), strArg(h, a[1])));
            }, this);
    }

    // -------------------------------------------------------- editor windows
    void registerEditorWindows()
    {
        if (!ctx->hasCapability(std::string(plugin::capability::editor)))
            return;
        ImGui::SetCurrentContext(ctx->getImGuiContext());
        abilityWindow = std::make_shared<gas::AbilityEditorWindow>();
        effectWindow = std::make_shared<gas::EffectEditorWindow>();
        tagWindow = std::make_shared<gas::TagTableEditorWindow>();
        debuggerWindow = std::make_shared<gas::GASDebuggerWindow>();
        ctx->registerEditorWindow(abilityWindow, "GAS Ability Editor");
        ctx->registerEditorWindow(effectWindow, "GAS Effect Editor");
        ctx->registerEditorWindow(tagWindow, "GAS Tag Table");
        ctx->registerEditorWindow(debuggerWindow, "GAS Debugger");
    }

    // Forward every gas.* PluginEventBus event into the runtime debugger.
    void registerDebuggerEvents()
    {
        static const char* const kEvents[] = {
            "gas.ability_activated", "gas.ability_ended", "gas.ability_failed",
            "gas.effect_applied", "gas.effect_removed", "gas.attribute_changed",
            "gas.tag_added", "gas.tag_removed"};
        for (const char* ev : kEvents)
        {
            const std::string name = ev;
            ctx->managedSubscribe(ctx->subscribeEvent(ev,
                [this, name](const nlohmann::json& data)
                {
                    if (debuggerWindow) debuggerWindow->record(name, data);
                }));
        }
    }

    plugin::PluginContext* ctx = nullptr;
    const MTypePluginHost* host = nullptr;
    gas::GASRuntime runtime;
    std::shared_ptr<gas::AbilityEditorWindow> abilityWindow;
    std::shared_ptr<gas::EffectEditorWindow> effectWindow;
    std::shared_ptr<gas::TagTableEditorWindow> tagWindow;
    std::shared_ptr<gas::GASDebuggerWindow> debuggerWindow;
};

VF_IMPLEMENT_PLUGIN(GameplayAbilitySystem)
