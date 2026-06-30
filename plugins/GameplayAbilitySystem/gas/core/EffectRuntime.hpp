#pragma once

// Gameplay Ability System (VK-816) — per-owner active-effect runtime ("EffectBook").
// ENGINE-FREE (see Tags.hpp header note).
//
// An EffectBook owns one owner's AttributeState, TagContainer and list of
// ActiveEffects. It is the only place effects are applied / ticked / removed.
// COOLDOWN and COST are NOT special-cased here: a cooldown is simply a Duration
// effect that grants a cooldown tag, and a cost is an Instant effect with
// negative modifiers — both flow through apply().
//
// Granted-tag lifetime: tags granted by effects are reference-counted inside the
// book, so two effects granting "Character.Stunned" keep the tag until both are
// gone, and a tag the user added directly is never dropped by effect expiry.

#include "Attributes.hpp"
#include "Aggregator.hpp"
#include "Effects.hpp"
#include "Tags.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gas
{
    class EffectBook
    {
    public:
        struct ApplyResult
        {
            bool applied = false;            // false => rejected (e.g. at stack limit)
            std::uint64_t handle = 0;        // 0 for Instant (not stored) or rejection
            std::vector<AttributeChange> changes;
        };

        struct RemoveResult
        {
            bool removed = false;
            std::vector<AttributeChange> changes;
        };

        struct TickReport
        {
            std::vector<AttributeChange> changes;   // net attribute deltas this tick
            std::vector<std::uint64_t> expired;     // handles fully removed this tick
            std::vector<PeriodicFire> periodicFires;
        };

        // --- owner state access -------------------------------------------------
        AttributeState& attributes() { return m_attributes; }
        const AttributeState& attributes() const { return m_attributes; }
        TagContainer& tags() { return m_tags; }
        const TagContainer& tags() const { return m_tags; }
        const std::vector<ActiveEffect>& active() const { return m_active; }

        // Recomputes currents from base + continuous mods. Useful after seeding
        // attributes directly; apply/tick/remove call it internally.
        std::vector<AttributeChange> recomputeAttributes()
        {
            return reaggregate();
        }

        // --- apply --------------------------------------------------------------
        ApplyResult apply(const GameplayEffectSpec& spec, std::uint64_t sourceId = 0)
        {
            ApplyResult r;

            if (spec.durationPolicy == DurationPolicy::Instant)
            {
                applyInstantToBase(spec, /*stacks*/ 1);
                r.changes = reaggregate();
                r.applied = true;
                r.handle = 0; // Instant effects are not stored
                return r;
            }

            const StackingConfig& st = spec.stacking;
            const int limit = st.limit > 0 ? st.limit : 1;

            if (st.policy == StackingPolicy::None)
            {
                int count = 0;
                for (const auto& ae : m_active)
                    if (ae.specId == spec.id)
                        ++count;
                if (count >= limit)
                {
                    r.applied = false; // reject at limit
                    return r;
                }
                r.handle = createActiveEffect(spec, sourceId);
                r.applied = true;
                r.changes = reaggregate();
                return r;
            }

            // BySource / ByTarget: find the matching stack.
            ActiveEffect* stack = nullptr;
            for (auto& ae : m_active)
            {
                if (ae.specId != spec.id)
                    continue;
                if (st.policy == StackingPolicy::BySource && ae.sourceId != sourceId)
                    continue;
                stack = &ae;
                break;
            }

            if (!stack)
            {
                r.handle = createActiveEffect(spec, sourceId);
                r.applied = true;
                r.changes = reaggregate();
                return r;
            }

            if (stack->stackCount < limit)
            {
                stack->stackCount += 1;
                if (st.durationRefresh && spec.durationPolicy == DurationPolicy::Duration)
                    stack->timeRemaining = spec.duration;
                r.handle = stack->handle;
                r.applied = true;
                r.changes = reaggregate(); // stackCount affects continuous mods
                return r;
            }

            // At limit. Refresh duration if configured; otherwise reject the extra application.
            if (st.durationRefresh && spec.durationPolicy == DurationPolicy::Duration)
            {
                stack->timeRemaining = spec.duration;
                r.handle = stack->handle;
                r.applied = true;
            }
            else
            {
                r.handle = stack->handle;
                r.applied = false;
            }
            return r;
        }

        // --- tick ---------------------------------------------------------------
        TickReport tick(float dt)
        {
            TickReport rep;
            std::vector<std::uint64_t> toExpire;

            for (auto& ae : m_active)
            {
                const bool isDuration = ae.spec.durationPolicy == DurationPolicy::Duration;
                const bool isPeriodic = ae.spec.period > 0.0f;

                if (isPeriodic)
                {
                    ae.periodAccumulator += dt;
                    while (ae.spec.period > 0.0f && ae.periodAccumulator >= ae.spec.period)
                    {
                        // Periodic modifiers fire against BASE, scaled by stack count.
                        applyInstantToBase(ae.spec, ae.stackCount);
                        rep.periodicFires.push_back({ae.handle, ae.specId, ae.stackCount});
                        ae.periodAccumulator -= ae.spec.period;
                    }
                }

                if (isDuration)
                {
                    ae.timeRemaining -= dt;
                    if (ae.timeRemaining <= 0.0f)
                        toExpire.push_back(ae.handle);
                }
                // Infinite never expires by time.
            }

            for (std::uint64_t h : toExpire)
            {
                auto it = findIter(h);
                if (it == m_active.end())
                    continue;
                ActiveEffect& ae = *it;

                const bool stackable = ae.spec.stacking.policy != StackingPolicy::None;
                if (stackable && ae.spec.stacking.expiration == StackExpiration::RemoveSingle &&
                    ae.stackCount > 1)
                {
                    // Drop one stack and refresh; effect persists (granted tags stay).
                    ae.stackCount -= 1;
                    ae.timeRemaining = ae.spec.duration;
                }
                else
                {
                    releaseTags(ae.spec.grantedTags);
                    rep.expired.push_back(ae.handle);
                    m_active.erase(it);
                }
            }

            // One reaggregation reports net change across the tick (periodic base
            // mutations + reverted continuous mods of expired effects).
            rep.changes = reaggregate();
            return rep;
        }

        // --- remove -------------------------------------------------------------
        RemoveResult remove(std::uint64_t handle)
        {
            RemoveResult r;
            auto it = findIter(handle);
            if (it == m_active.end())
                return r;
            releaseTags(it->spec.grantedTags);
            m_active.erase(it);
            r.removed = true;
            r.changes = reaggregate();
            return r;
        }

        // --- cooldown query -----------------------------------------------------
        // Max timeRemaining among active Duration effects whose granted tags
        // hierarchically satisfy any of the supplied cooldown tags. 0 => not on cooldown.
        float getCooldownRemaining(const std::vector<std::string>& cooldownTags) const
        {
            float maxRem = 0.0f;
            for (const auto& ae : m_active)
            {
                if (ae.spec.durationPolicy != DurationPolicy::Duration)
                    continue;
                TagContainer granted;
                for (const auto& t : ae.spec.grantedTags)
                    granted.addTag(t);
                bool match = false;
                for (const auto& ct : cooldownTags)
                    if (granted.hasTag(ct))
                    {
                        match = true;
                        break;
                    }
                if (match)
                    maxRem = std::max(maxRem, ae.timeRemaining);
            }
            return maxRem;
        }

        // Synthesizes a Duration cooldown effect that grants the given tags. Handy
        // when an ability declares a cooldown by duration+tags rather than effectId.
        static GameplayEffectSpec makeCooldownEffect(const std::string& id, float duration,
                                                     const std::vector<std::string>& cooldownTags)
        {
            GameplayEffectSpec ge;
            ge.id = id;
            ge.displayName = id;
            ge.durationPolicy = DurationPolicy::Duration;
            ge.duration = duration;
            ge.grantedTags = cooldownTags;
            return ge;
        }

    private:
        std::vector<ActiveEffect>::iterator findIter(std::uint64_t handle)
        {
            return std::find_if(m_active.begin(), m_active.end(),
                                [handle](const ActiveEffect& a) { return a.handle == handle; });
        }

        std::uint64_t createActiveEffect(const GameplayEffectSpec& spec, std::uint64_t sourceId)
        {
            ActiveEffect ae;
            ae.handle = m_nextHandle++;
            ae.specId = spec.id;
            ae.spec = spec; // owned copy
            ae.timeRemaining = spec.durationPolicy == DurationPolicy::Duration ? spec.duration : 0.0f;
            ae.periodAccumulator = 0.0f;
            ae.stackCount = 1;
            ae.sourceId = sourceId;
            m_active.push_back(std::move(ae));
            grantTags(spec.grantedTags);
            return m_active.back().handle;
        }

        // Applies an Instant/periodic spec's modifiers to BASE (scaled by `stacks`),
        // clamping base into its resolved bounds after each modifier.
        void applyInstantToBase(const GameplayEffectSpec& spec, int stacks)
        {
            const int n = stacks > 0 ? stacks : 1;
            for (int s = 0; s < n; ++s)
            {
                for (const auto& m : spec.modifiers)
                {
                    Attribute& a = m_attributes.ensure(m.attribute);
                    switch (m.op)
                    {
                    case ModifierOp::Add:
                        a.base += m.magnitude;
                        break;
                    case ModifierOp::Multiply:
                        a.base *= m.magnitude;
                        break;
                    case ModifierOp::Override:
                        a.base = m.magnitude;
                        break;
                    }
                    const auto bounds = resolveAttributeBounds(m_attributes, a);
                    a.base = clampValue(a.base, bounds.first, bounds.second);
                }
            }
        }

        // Continuous modifiers: every active, non-Instant, non-periodic, non-inhibited
        // effect contributes its modifiers once per stack.
        std::vector<AttributeModifier> gatherContinuousMods() const
        {
            std::vector<AttributeModifier> out;
            for (const auto& ae : m_active)
            {
                if (ae.spec.durationPolicy == DurationPolicy::Instant)
                    continue;
                if (ae.spec.period > 0.0f)
                    continue;
                if (ae.inhibited)
                    continue;
                for (int s = 0; s < ae.stackCount; ++s)
                    for (const auto& m : ae.spec.modifiers)
                        out.push_back(m);
            }
            return out;
        }

        std::vector<AttributeChange> reaggregate()
        {
            return Aggregator::recompute(m_attributes, gatherContinuousMods());
        }

        void grantTags(const std::vector<std::string>& tags)
        {
            for (const auto& t : tags)
            {
                if (t.empty())
                    continue;
                int& c = m_grantedTagCounts[t];
                if (c == 0)
                    m_tags.addTag(t);
                ++c;
            }
        }

        void releaseTags(const std::vector<std::string>& tags)
        {
            for (const auto& t : tags)
            {
                if (t.empty())
                    continue;
                auto it = m_grantedTagCounts.find(t);
                if (it == m_grantedTagCounts.end())
                    continue;
                if (--(it->second) <= 0)
                {
                    m_tags.removeTag(t);
                    m_grantedTagCounts.erase(it);
                }
            }
        }

        AttributeState m_attributes;
        TagContainer m_tags;
        std::vector<ActiveEffect> m_active;
        std::map<std::string, int> m_grantedTagCounts;
        std::uint64_t m_nextHandle = 1;
    };
}
