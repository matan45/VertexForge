#pragma once

// Gameplay Ability System (VK-816) — runtime orchestrator (plugin-side glue).
//
// Owns the per-entity gameplay state (one gas::EffectBook + granted abilities
// each) keyed by entity id, plus the asset caches. Bridges the engine-free
// gas/core logic to the EnTT registry, input, VFX/audio cues and the gas.*
// plugin event bus. The native _gas_* functions and onUpdate both drive it.

#include "../core/ActivationPipeline.hpp"
#include "../core/EffectRuntime.hpp"
#include "../core/GASAssets.hpp"
#include "../core/CueDispatch.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace plugin { class PluginContext; }

namespace gas
{
    class GASRuntime
    {
    public:
        // Sink that realises a resolved cue (production: spawn VFX + audio;
        // tests: record). Set by the plugin in onInitialize.
        using CueRealizer = std::function<void(const CueEvent&)>;
        // Publishes a gas.* plugin event (name + json payload). Set by the plugin.
        using EventEmitter = std::function<void(const std::string&, const nlohmann::json&)>;

        void init(plugin::PluginContext* context, CueRealizer cueSink, EventEmitter emit);

        void setGameActive(bool active);
        bool isGameActive() const { return gameActive; }

        // Per-frame: tick active effects + refresh component mirrors, and — when
        // the game is active — poll input to auto-activate granted abilities.
        // dt is the scaled game delta (0 outside play => effects are paused).
        void tick(float dt);

        // Drop a despawned entity's state (call when an entity is destroyed).
        void forget(std::uint32_t entity) { entities.erase(entity); }
        void reset();

        // --- Native-facing operations (entity ids are raw uint32 from scripts) ---
        int grantAbility(std::uint32_t entity, const std::string& abilityPath);
        int revokeAbility(std::uint32_t entity, const std::string& abilityId);
        ActivationStatus canActivate(std::uint32_t entity, const std::string& abilityId, std::int64_t target);
        ActivationStatus activate(std::uint32_t entity, const std::string& abilityId, std::int64_t target);
        int setTarget(std::uint32_t entity, std::int64_t target);
        void cancelAbility(std::uint32_t entity, std::uint64_t handle);
        std::uint64_t applyEffect(std::uint32_t source, std::uint32_t target, const std::string& effectPath);
        bool removeEffect(std::uint32_t entity, std::uint64_t handle);
        float getAttribute(std::uint32_t entity, const std::string& name);
        float getAttributeBase(std::uint32_t entity, const std::string& name);
        bool hasTag(std::uint32_t entity, const std::string& tag);
        bool hasAllTags(std::uint32_t entity, const std::vector<std::string>& tags);
        bool hasAnyTags(std::uint32_t entity, const std::vector<std::string>& tags);
        std::vector<std::string> getTags(std::uint32_t entity);
        float getCooldownRemaining(std::uint32_t entity, const std::string& abilityId);

    private:
        struct EntityState
        {
            EffectBook book;
            std::unordered_set<std::string> grantedAbilityIds;
            std::unordered_map<std::string, std::string> abilityPathById; // id -> .vfAbility path
            bool seeded = false;
        };

        EntityState& getOrSeed(std::uint32_t entity);
        void seedFromComponents(entt::entity e, EntityState& st);
        void refreshMirrors(entt::entity e, EntityState& st);

        // Resolve a project-relative reference path to an absolute path via the
        // engine (project-root aware), so GAS assets load regardless of CWD.
        std::string resolvePath(const std::string& path) const;
        const AbilitySpec* loadAbility(const std::string& path);
        const GameplayEffectSpec* resolveEffect(const std::string& idOrPath);
        const GameplayCueSpec* resolveCue(const std::string& idOrPath);
        float cooldownRemainingFor(EntityState& st, const AbilitySpec& spec);
        void clearSpecCaches();
        void buildEffectIdIndex();
        void buildCueIdIndex();

        void emitCues(std::uint32_t entity, const std::vector<std::string>& cueIds);
        void emitAttributeChanges(std::uint32_t entity, const std::vector<AttributeChange>& changes);
        bool worldPositionOf(std::uint32_t entity, float& x, float& y, float& z) const;

        plugin::PluginContext* ctx = nullptr;
        CueRealizer cueRealizer;
        EventEmitter emitEvent;
        bool gameActive = false;

        std::unordered_map<std::uint32_t, EntityState> entities;
        std::unordered_map<std::string, AbilitySpec> abilityCache;       // by path
        std::unordered_map<std::string, GameplayEffectSpec> effectCache; // by path and by id
        std::unordered_map<std::string, GameplayCueSpec> cueCache;       // by path and by id
        std::unordered_map<std::string, std::string> effectIdToPath;
        std::unordered_map<std::string, std::string> cueIdToPath;
        bool effectIdIndexBuilt = false;
        bool cueIdIndexBuilt = false;
    };
}
