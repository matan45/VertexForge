#include "GASRuntime.hpp"
#include "../components/GASComponents.hpp"

#include "api/PluginContext.hpp"
#include "components/CoreComponents.hpp"

#include <glm/glm.hpp>

#include <limits>
#include <string>
#include <utility>

namespace gas
{
    void GASRuntime::init(plugin::PluginContext* context, CueRealizer cueSink, EventEmitter emit)
    {
        ctx = context;
        cueRealizer = std::move(cueSink);
        emitEvent = std::move(emit);
    }

    // ------------------------------------------------------------------ assets
    // unordered_map element pointers/references are stable across rehash (only
    // iterators invalidate), so a captured `&value` survives a later insert.

    std::string GASRuntime::resolvePath(const std::string& path) const
    {
        // Project-root aware (handles any project / working directory). Returns
        // the input unchanged when no engine context / project is available.
        return ctx ? ctx->resolveProjectPath(path) : path;
    }

    const AbilitySpec* GASRuntime::loadAbility(const std::string& path)
    {
        if (auto it = abilityCache.find(path); it != abilityCache.end()) return &it->second;
        auto spec = AbilityAsset::load(resolvePath(path));
        if (!spec) spec = AbilityAsset::load(path);   // fallback: load as-is
        if (!spec) return nullptr;
        auto [ins, ok] = abilityCache.emplace(path, std::move(*spec));
        return &ins->second;
    }

    const GameplayEffectSpec* GASRuntime::resolveEffect(const std::string& idOrPath)
    {
        if (auto it = effectCache.find(idOrPath); it != effectCache.end()) return &it->second;
        auto spec = EffectAsset::load(resolvePath(idOrPath));
        if (!spec) spec = EffectAsset::load(idOrPath);
        if (!spec) return nullptr;
        const std::string id = spec->id;
        auto [ins, ok] = effectCache.emplace(idOrPath, std::move(*spec));
        GameplayEffectSpec* result = &ins->second;          // stable across the next insert
        if (!id.empty() && id != idOrPath) effectCache.try_emplace(id, *result);
        return result;
    }

    const GameplayCueSpec* GASRuntime::resolveCue(const std::string& idOrPath)
    {
        if (auto it = cueCache.find(idOrPath); it != cueCache.end()) return &it->second;
        auto spec = CueAsset::load(resolvePath(idOrPath));
        if (!spec) spec = CueAsset::load(idOrPath);
        if (!spec) return nullptr;
        const std::string id = spec->id;
        auto [ins, ok] = cueCache.emplace(idOrPath, std::move(*spec));
        GameplayCueSpec* result = &ins->second;
        if (!id.empty() && id != idOrPath) cueCache.try_emplace(id, *result);
        return result;
    }

    // ----------------------------------------------------------------- seeding
    GASRuntime::EntityState& GASRuntime::getOrSeed(std::uint32_t entity)
    {
        auto& st = entities[entity];
        if (!st.seeded)
        {
            seedFromComponents(static_cast<entt::entity>(entity), st);
            st.seeded = true;
        }
        return st;
    }

    void GASRuntime::seedFromComponents(entt::entity e, EntityState& st)
    {
        if (!ctx) return;
        auto& reg = ctx->getRegistry();
        if (!reg.valid(e)) return;

        if (const auto* attrs = reg.try_get<GAS_AttributeSetComponent>(e))
        {
            const std::size_t n = attrs->attributeNames.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                Attribute a;
                a.base     = i < attrs->baseValues.size() ? attrs->baseValues[i] : 0.0f;
                a.minValue = i < attrs->minValues.size() ? attrs->minValues[i]
                                                         : -std::numeric_limits<float>::infinity();
                a.maxValue = i < attrs->maxValues.size() ? attrs->maxValues[i]
                                                         : std::numeric_limits<float>::infinity();
                st.book.attributes().setAttribute(attrs->attributeNames[i], a);
            }
            st.book.recomputeAttributes();
        }

        if (const auto* tags = reg.try_get<GAS_TagComponent>(e))
            for (const auto& t : tags->looseTags)
                st.book.tags().addTag(t);

        if (const auto* abilSys = reg.try_get<GAS_AbilitySystemComponent>(e))
            for (const auto& ref : abilSys->grantedAbilities)
            {
                const std::string path = ctx->resolveAssetPath(ref.toHexString());
                if (path.empty()) continue;
                if (const AbilitySpec* spec = loadAbility(path))
                {
                    st.grantedAbilityIds.insert(spec->id);
                    st.abilityPathById[spec->id] = path;
                }
            }

        if (const auto* eff = reg.try_get<GAS_ActiveEffectsComponent>(e))
            for (const auto& ref : eff->startupEffects)
            {
                const std::string path = ctx->resolveAssetPath(ref.toHexString());
                if (path.empty()) continue;
                if (const GameplayEffectSpec* spec = resolveEffect(path))
                    st.book.apply(*spec, static_cast<std::uint64_t>(e));
            }
    }

    // ---------------------------------------------------------------- abilities
    int GASRuntime::grantAbility(std::uint32_t entity, const std::string& abilityPath)
    {
        const AbilitySpec* spec = loadAbility(abilityPath);
        if (!spec) return 0;
        EntityState& st = getOrSeed(entity);
        st.grantedAbilityIds.insert(spec->id);
        st.abilityPathById[spec->id] = abilityPath;
        return 1;
    }

    int GASRuntime::revokeAbility(std::uint32_t entity, const std::string& abilityId)
    {
        auto it = entities.find(entity);
        if (it == entities.end()) return 0;
        const int erased = static_cast<int>(it->second.grantedAbilityIds.erase(abilityId));
        it->second.abilityPathById.erase(abilityId);
        return erased;
    }

    ActivationStatus GASRuntime::canActivate(std::uint32_t entity, const std::string& abilityId, std::int64_t target)
    {
        EntityState& st = getOrSeed(entity);
        auto pit = st.abilityPathById.find(abilityId);
        if (!st.grantedAbilityIds.count(abilityId) || pit == st.abilityPathById.end())
            return ActivationStatus::NotGranted;
        const AbilitySpec* spec = loadAbility(pit->second);
        if (!spec) return ActivationStatus::Internal;

        ActivationContext c;
        c.ability = spec;
        c.ownerTags = &st.book.tags();
        c.ownerAttributes = &st.book.attributes();
        c.isGranted = true;
        c.cooldownRemaining = st.book.getCooldownRemaining(spec->cooldown.cooldownTags);
        c.resolveEffect = [this](const std::string& id) { return resolveEffect(id); };
        if (target >= 0)
        {
            c.target.hasTarget = true;
            c.target.tags = getOrSeed(static_cast<std::uint32_t>(target)).book.tags();
        }
        return ActivationPipeline::planActivate(c).status;
    }

    ActivationStatus GASRuntime::activate(std::uint32_t entity, const std::string& abilityId, std::int64_t target)
    {
        EntityState& st = getOrSeed(entity);
        auto pit = st.abilityPathById.find(abilityId);
        if (!st.grantedAbilityIds.count(abilityId) || pit == st.abilityPathById.end())
        {
            if (emitEvent)
                emitEvent("gas.ability_failed",
                          {{"entity", static_cast<int>(entity)}, {"ability", abilityId}, {"status", static_cast<int>(ActivationStatus::NotGranted)}});
            return ActivationStatus::NotGranted;
        }
        const AbilitySpec* spec = loadAbility(pit->second);
        if (!spec) return ActivationStatus::Internal;

        ActivationContext c;
        c.ability = spec;
        c.ownerTags = &st.book.tags();
        c.ownerAttributes = &st.book.attributes();
        c.isGranted = true;
        c.cooldownRemaining = st.book.getCooldownRemaining(spec->cooldown.cooldownTags);
        c.resolveEffect = [this](const std::string& id) { return resolveEffect(id); };
        if (target >= 0)
        {
            c.target.hasTarget = true;
            c.target.tags = getOrSeed(static_cast<std::uint32_t>(target)).book.tags();
        }

        const ActivationPlan plan = ActivationPipeline::planActivate(c);
        if (!plan.valid)
        {
            if (emitEvent)
                emitEvent("gas.ability_failed",
                          {{"entity", static_cast<int>(entity)}, {"ability", abilityId}, {"status", static_cast<int>(plan.status)}});
            return plan.status;
        }

        // unordered_map references are stable across the getOrSeed(target) insert
        // above, so `st` is still valid here.
        const CommitResult res = ActivationPipeline::commit(plan, st.book, static_cast<std::uint64_t>(entity));

        if (emitEvent)
        {
            emitEvent("gas.ability_activated",
                      {{"entity", static_cast<int>(entity)}, {"ability", abilityId}, {"target", static_cast<int>(target)}});
            for (const auto h : res.effectHandles)
                if (h != 0)
                    emitEvent("gas.effect_applied",
                              {{"entity", static_cast<int>(entity)}, {"handle", static_cast<int>(h)}, {"source", static_cast<int>(entity)}});
        }
        emitAttributeChanges(entity, res.attributeChanges);
        emitCues(entity, res.cuesToDispatch);
        refreshMirrors(static_cast<entt::entity>(entity), st);
        return ActivationStatus::Success;
    }

    void GASRuntime::cancelAbility(std::uint32_t /*entity*/, std::uint64_t /*handle*/)
    {
        // v1: abilities in the vertical slice complete synchronously (no long-
        // running ability instances are tracked by handle), so cancel is a
        // documented no-op. Cancellation of in-flight task sequences is a
        // deferred enhancement.
    }

    // ------------------------------------------------------------------ effects
    std::uint64_t GASRuntime::applyEffect(std::uint32_t source, std::uint32_t target, const std::string& effectPath)
    {
        const GameplayEffectSpec* spec = resolveEffect(effectPath);
        if (!spec) return 0;
        EntityState& st = getOrSeed(target);
        const EffectBook::ApplyResult r = st.book.apply(*spec, source);
        if (emitEvent && r.applied)
            emitEvent("gas.effect_applied",
                      {{"entity", static_cast<int>(target)}, {"handle", static_cast<int>(r.handle)}, {"source", static_cast<int>(source)}});
        emitAttributeChanges(target, r.changes);
        emitCues(target, spec->cueIds);
        refreshMirrors(static_cast<entt::entity>(target), st);
        return r.handle;
    }

    bool GASRuntime::removeEffect(std::uint32_t entity, std::uint64_t handle)
    {
        auto it = entities.find(entity);
        if (it == entities.end()) return false;
        const EffectBook::RemoveResult r = it->second.book.remove(handle);
        if (r.removed && emitEvent)
            emitEvent("gas.effect_removed",
                      {{"entity", static_cast<int>(entity)}, {"handle", static_cast<int>(handle)}, {"reason", "Removed"}});
        emitAttributeChanges(entity, r.changes);
        refreshMirrors(static_cast<entt::entity>(entity), it->second);
        return r.removed;
    }

    // --------------------------------------------------------------- attributes
    float GASRuntime::getAttribute(std::uint32_t entity, const std::string& name)
    {
        return getOrSeed(entity).book.attributes().currentOf(name);
    }

    float GASRuntime::getAttributeBase(std::uint32_t entity, const std::string& name)
    {
        return getOrSeed(entity).book.attributes().baseOf(name);
    }

    // --------------------------------------------------------------------- tags
    bool GASRuntime::hasTag(std::uint32_t entity, const std::string& tag)
    {
        return getOrSeed(entity).book.tags().hasTag(tag);
    }

    bool GASRuntime::hasAllTags(std::uint32_t entity, const std::vector<std::string>& tags)
    {
        return getOrSeed(entity).book.tags().hasAll(tags);
    }

    bool GASRuntime::hasAnyTags(std::uint32_t entity, const std::vector<std::string>& tags)
    {
        return getOrSeed(entity).book.tags().hasAny(tags);
    }

    std::vector<std::string> GASRuntime::getTags(std::uint32_t entity)
    {
        std::vector<std::string> out;
        for (const auto& t : getOrSeed(entity).book.tags().tags())
            out.push_back(t);
        return out;
    }

    float GASRuntime::getCooldownRemaining(std::uint32_t entity, const std::string& abilityId)
    {
        EntityState& st = getOrSeed(entity);
        auto pit = st.abilityPathById.find(abilityId);
        if (pit == st.abilityPathById.end()) return 0.0f;
        const AbilitySpec* spec = loadAbility(pit->second);
        if (!spec) return 0.0f;
        return st.book.getCooldownRemaining(spec->cooldown.cooldownTags);
    }

    // --------------------------------------------------------------------- tick
    void GASRuntime::tick(float dt)
    {
        if (!ctx) return;
        auto& reg = ctx->getRegistry();

        // When playing, discover and seed EVERY entity carrying any GAS component
        // (seeds attributes/tags and applies startup effects). Without this an
        // entity that has only GAS_AttributeSet + GAS_ActiveEffects (no ability
        // system) would never be seeded. getOrSeed is idempotent.
        if (gameActive)
        {
            for (const auto e : reg.view<GAS_AttributeSetComponent>()) getOrSeed(static_cast<std::uint32_t>(e));
            for (const auto e : reg.view<GAS_ActiveEffectsComponent>()) getOrSeed(static_cast<std::uint32_t>(e));
            for (const auto e : reg.view<GAS_AbilitySystemComponent>()) getOrSeed(static_cast<std::uint32_t>(e));
        }

        // Advance effects on every seeded entity. dt is the scaled game delta
        // (0 outside Play), so timed effects/cooldowns naturally pause in edit.
        for (auto& [id, st] : entities)
        {
            const EffectBook::TickReport rep = st.book.tick(dt);
            if (emitEvent)
                for (const auto h : rep.expired)
                    emitEvent("gas.effect_removed",
                              {{"entity", static_cast<int>(id)}, {"handle", static_cast<int>(h)}, {"reason", "Expired"}});
            emitAttributeChanges(id, rep.changes);
            refreshMirrors(static_cast<entt::entity>(id), st);
        }

        if (!gameActive) return;

        // Input-driven activation (Play mode only). Poll each ability-bearing
        // entity's granted OnPressed abilities and fire bound input actions.
        for (const auto e : reg.view<GAS_AbilitySystemComponent>())
        {
            const auto& abilSys = reg.get<GAS_AbilitySystemComponent>(e);
            if (!abilSys.pollInput) continue;
            const auto id = static_cast<std::uint32_t>(e);
            EntityState& st = getOrSeed(id);
            for (const auto& abilityId : st.grantedAbilityIds)
            {
                auto pit = st.abilityPathById.find(abilityId);
                if (pit == st.abilityPathById.end()) continue;
                const AbilitySpec* spec = loadAbility(pit->second);
                if (!spec) continue;
                if (spec->activation.policy != ActivationPolicy::OnPressed) continue;
                if (spec->activation.inputAction.empty()) continue;
                if (ctx->isActionPressed(spec->activation.inputAction))
                    activate(id, abilityId, /*target*/ -1);
            }
        }
    }

    // ------------------------------------------------------------------ helpers
    void GASRuntime::emitAttributeChanges(std::uint32_t entity, const std::vector<AttributeChange>& changes)
    {
        if (!emitEvent) return;
        for (const auto& c : changes)
            emitEvent("gas.attribute_changed",
                      {{"entity", static_cast<int>(entity)}, {"attribute", c.attribute}, {"oldValue", c.oldValue}, {"newValue", c.newValue}});
    }

    void GASRuntime::emitCues(std::uint32_t entity, const std::vector<std::string>& cueIds)
    {
        if (cueIds.empty() || !cueRealizer) return;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        worldPositionOf(entity, x, y, z);
        const CueResolver resolve = [this](const std::string& id) { return resolveCue(id); };
        dispatchCues(cueIds, resolve, cueRealizer, x, y, z);
    }

    bool GASRuntime::worldPositionOf(std::uint32_t entity, float& x, float& y, float& z) const
    {
        if (!ctx) return false;
        auto& reg = ctx->getRegistry();
        const auto e = static_cast<entt::entity>(entity);
        if (!reg.valid(e)) return false;
        if (const auto* w = reg.try_get<components::WorldTransformComponent>(e))
        {
            x = w->worldMatrix[3][0];
            y = w->worldMatrix[3][1];
            z = w->worldMatrix[3][2];
            return true;
        }
        if (const auto* t = reg.try_get<components::TransformComponent>(e))
        {
            x = t->position.x;
            y = t->position.y;
            z = t->position.z;
            return true;
        }
        return false;
    }

    void GASRuntime::refreshMirrors(entt::entity e, EntityState& st)
    {
        if (!ctx) return;
        auto& reg = ctx->getRegistry();
        if (!reg.valid(e)) return;

        if (auto* attrs = reg.try_get<GAS_AttributeSetComponent>(e))
        {
            attrs->currentValues.assign(attrs->attributeNames.size(), 0.0f);
            for (std::size_t i = 0; i < attrs->attributeNames.size(); ++i)
                attrs->currentValues[i] = st.book.attributes().currentOf(attrs->attributeNames[i]);
        }
        if (auto* tags = reg.try_get<GAS_TagComponent>(e))
        {
            tags->grantedTags.clear();
            for (const auto& t : st.book.tags().tags())
                tags->grantedTags.push_back(t);
        }
        if (auto* eff = reg.try_get<GAS_ActiveEffectsComponent>(e))
        {
            eff->activeEffectSummary.clear();
            for (const auto& ae : st.book.active())
                eff->activeEffectSummary.push_back(ae.specId + " x" + std::to_string(ae.stackCount));
        }
    }
}
