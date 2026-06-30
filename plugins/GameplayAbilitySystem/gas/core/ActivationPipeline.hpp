#pragma once

// Gameplay Ability System (VK-816) — transactional ability activation.
// ENGINE-FREE (see Tags.hpp header note).
//
// planActivate() is PURE: it reads an ActivationContext (a view over owner tags,
// owner attributes, granted/cooldown queries, an effect resolver and an optional
// target) and either returns a fully-built ActivationPlan (status Success) or, on
// the FIRST failed check, a failure status with an INVALID/empty plan. It never
// mutates anything, so a caller can snapshot the owner, plan, and find the owner
// byte-identical regardless of outcome.
//
// commit() applies a valid plan to the owner's EffectBook in order: cost ->
// cooldown -> effectsOnActivate -> activationOwnedTags, returning cues/tasks/cancel
// requests for the engine to dispatch. Committing an INVALID plan mutates nothing.
//
// Check order (UE-style): granted -> blocked tags -> required tags -> cooldown ->
// cost (dry-run against CURRENT) -> target.

#include "AbilitySpec.hpp"
#include "Attributes.hpp"
#include "Effects.hpp"
#include "EffectRuntime.hpp"
#include "Tags.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gas
{
    enum class ActivationStatus
    {
        Success = 0,
        InvalidEntity,
        NotGranted,
        BlockedByTags,
        MissingTags,
        OnCooldown,
        CostNotMet,
        NoTarget,
        Internal
    };

    // Optional target view. `tags` are the target's owned tags.
    struct TargetInfo
    {
        bool hasTarget = false;
        TagContainer tags;
    };

    // A read-only view supplied by the engine. Resolver maps effectId -> spec*;
    // returning nullptr for a referenced id is treated as a config error (Internal).
    struct ActivationContext
    {
        const AbilitySpec* ability = nullptr;
        const TagContainer* ownerTags = nullptr;
        const AttributeState* ownerAttributes = nullptr;
        bool isGranted = false;
        float cooldownRemaining = 0.0f; // > 0 => on cooldown
        std::function<const GameplayEffectSpec*(const std::string&)> resolveEffect;
        TargetInfo target;
    };

    // Plain data describing what to commit. Effect specs are stored BY VALUE so
    // the plan is self-contained and safe to copy/move (no dangling resolver ptrs).
    struct ActivationPlan
    {
        bool valid = false;
        ActivationStatus status = ActivationStatus::Internal;

        std::optional<GameplayEffectSpec> costEffect;
        std::optional<GameplayEffectSpec> cooldownEffect;
        std::vector<GameplayEffectSpec> effectsOnActivate;
        std::vector<std::string> ownedTagsToAdd;
        std::vector<AbilityTask> tasks;
        std::vector<std::string> cueIds;
        std::vector<std::string> cancelAbilitiesWithTag;
    };

    struct CommitResult
    {
        std::vector<AttributeChange> attributeChanges;
        std::uint64_t costHandle = 0;
        std::uint64_t cooldownHandle = 0;
        std::vector<std::uint64_t> effectHandles;
        std::vector<std::string> ownedTagsAdded;
        std::vector<std::string> cuesToDispatch;
        std::vector<AbilityTask> tasksToRun;
        std::vector<std::string> cancelAbilitiesWithTag;
    };

    namespace detail
    {
        // Dry-run a cost effect against a copy of CURRENT values; returns false if
        // any modified attribute would drop below its (resolved-static) min.
        inline bool costAffordable(const GameplayEffectSpec& cost, const AttributeState& attrs)
        {
            for (const auto& m : cost.modifiers)
            {
                const Attribute* a = attrs.find(m.attribute);
                const float cur = a ? a->current : 0.0f;
                const float lo = a ? a->minValue : 0.0f;
                float result = cur;
                switch (m.op)
                {
                case ModifierOp::Add:
                    result = cur + m.magnitude;
                    break;
                case ModifierOp::Multiply:
                    result = cur * m.magnitude;
                    break;
                case ModifierOp::Override:
                    result = m.magnitude;
                    break;
                }
                if (result < lo)
                    return false;
            }
            return true;
        }

        inline ActivationPlan fail(ActivationStatus status)
        {
            ActivationPlan p;
            p.valid = false;
            p.status = status;
            return p;
        }
    }

    class ActivationPipeline
    {
    public:
        // PURE: builds a plan iff every check passes; otherwise returns an empty,
        // invalid plan carrying the failure status. Never mutates.
        static ActivationPlan planActivate(const ActivationContext& ctx)
        {
            if (!ctx.ability || !ctx.ownerTags || !ctx.ownerAttributes)
                return detail::fail(ActivationStatus::InvalidEntity);

            const AbilitySpec& ab = *ctx.ability;
            const TagContainer& owner = *ctx.ownerTags;

            // 1) granted
            if (!ctx.isGranted)
                return detail::fail(ActivationStatus::NotGranted);

            // 2) blocked tags (disjoint from owner)
            if (owner.hasAny(ab.activationBlockedTags))
                return detail::fail(ActivationStatus::BlockedByTags);

            // 3) required tags (subset of owner)
            if (!owner.hasAll(ab.activationRequiredTags))
                return detail::fail(ActivationStatus::MissingTags);

            // 4) cooldown
            if (ctx.cooldownRemaining > 0.0f)
                return detail::fail(ActivationStatus::OnCooldown);

            // Resolve cost effect (if any) up front for the dry-run + plan.
            std::optional<GameplayEffectSpec> costEffect;
            if (!ab.cost.effectId.empty())
            {
                const GameplayEffectSpec* cost = ctx.resolveEffect ? ctx.resolveEffect(ab.cost.effectId) : nullptr;
                if (!cost)
                    return detail::fail(ActivationStatus::Internal);
                costEffect = *cost;
            }

            // 5) cost (dry-run against CURRENT)
            if (costEffect && !detail::costAffordable(*costEffect, *ctx.ownerAttributes))
                return detail::fail(ActivationStatus::CostNotMet);

            // 6) target
            if (ab.targeting.type != TargetingType::Self)
            {
                if (!ctx.target.hasTarget)
                    return detail::fail(ActivationStatus::NoTarget);
                if (!ctx.target.tags.hasAll(ab.targetRequiredTags))
                    return detail::fail(ActivationStatus::NoTarget);
            }

            // Resolve cooldown effect: explicit effectId wins; else synthesize from
            // duration + cooldownTags. Either may be absent (no cooldown).
            std::optional<GameplayEffectSpec> cooldownEffect;
            if (!ab.cooldown.effectId.empty())
            {
                const GameplayEffectSpec* cd = ctx.resolveEffect ? ctx.resolveEffect(ab.cooldown.effectId) : nullptr;
                if (!cd)
                    return detail::fail(ActivationStatus::Internal);
                cooldownEffect = *cd;
            }
            else if (ab.cooldown.duration > 0.0f)
            {
                cooldownEffect = EffectBook::makeCooldownEffect(ab.id + ".Cooldown",
                                                                ab.cooldown.duration,
                                                                ab.cooldown.cooldownTags);
            }

            // Resolve the activate effects.
            std::vector<GameplayEffectSpec> effects;
            effects.reserve(ab.effectsOnActivate.size());
            for (const auto& eid : ab.effectsOnActivate)
            {
                const GameplayEffectSpec* e = ctx.resolveEffect ? ctx.resolveEffect(eid) : nullptr;
                if (!e)
                    return detail::fail(ActivationStatus::Internal);
                effects.push_back(*e);
            }

            // All checks passed — build the committed plan.
            ActivationPlan plan;
            plan.valid = true;
            plan.status = ActivationStatus::Success;
            plan.costEffect = std::move(costEffect);
            plan.cooldownEffect = std::move(cooldownEffect);
            plan.effectsOnActivate = std::move(effects);
            plan.ownedTagsToAdd = ab.activationOwnedTags;
            plan.tasks = ab.tasks;
            plan.cueIds = ab.cueIds;
            plan.cancelAbilitiesWithTag = ab.cancelAbilitiesWithTag;
            return plan;
        }

        // Applies a valid plan to the owner's EffectBook (cost -> cooldown ->
        // effects -> owned tags). An invalid plan is a no-op. `selfSourceId` tags
        // the applied effects with the owner as their source.
        static CommitResult commit(const ActivationPlan& plan, EffectBook& book, std::uint64_t selfSourceId = 0)
        {
            CommitResult result;
            if (!plan.valid)
                return result; // empty plan mutates nothing

            auto appendChanges = [&result](std::vector<AttributeChange>& src)
            {
                for (auto& c : src)
                    result.attributeChanges.push_back(std::move(c));
            };

            if (plan.costEffect)
            {
                auto r = book.apply(*plan.costEffect, selfSourceId);
                result.costHandle = r.handle;
                appendChanges(r.changes);
            }
            if (plan.cooldownEffect)
            {
                auto r = book.apply(*plan.cooldownEffect, selfSourceId);
                result.cooldownHandle = r.handle;
                appendChanges(r.changes);
            }
            for (const auto& e : plan.effectsOnActivate)
            {
                auto r = book.apply(e, selfSourceId);
                result.effectHandles.push_back(r.handle);
                appendChanges(r.changes);
            }
            for (const auto& t : plan.ownedTagsToAdd)
            {
                book.tags().addTag(t);
                result.ownedTagsAdded.push_back(t);
            }

            result.cuesToDispatch = plan.cueIds;
            result.tasksToRun = plan.tasks;
            result.cancelAbilitiesWithTag = plan.cancelAbilitiesWithTag;
            return result;
        }
    };
}
