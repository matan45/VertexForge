#pragma once

// Gameplay Ability System (VK-816) — attribute aggregation.
// ENGINE-FREE (see Tags.hpp header note).
//
// The Aggregator is the SOLE writer of Attribute::current during gameplay.
// For each attribute it computes, in this deterministic UE-style order:
//   1. resolve the effective min/max bounds (possibly driven by another
//      attribute's current value — those provider attributes are computed first),
//   2. sum the Add modifiers,
//   3. take the product of the Multiply modifiers (identity 1.0),
//   4. apply Override (last-applied override value wins, ignoring add/mul),
//   5. clamp into [min, max].
// Formula: current = clamp( hasOverride ? overrideVal : (base + sumAdd) * prodMul, min, max ).
//
// It returns only the attributes whose current actually CHANGED so callers can
// emit change events on delta alone.

#include "Attributes.hpp"

#include <map>
#include <string>
#include <vector>

namespace gas
{
    class Aggregator
    {
    public:
        // Recomputes current for every attribute in `state` from its base plus the
        // supplied continuous modifiers. Returns {attribute, old, new} for changes.
        static std::vector<AttributeChange> recompute(AttributeState& state,
                                                       const std::vector<AttributeModifier>& mods)
        {
            // --- Pass 1: accumulate per-attribute Add/Multiply/Override in mod order. ---
            struct Accum
            {
                float sumAdd = 0.0f;
                float prodMul = 1.0f;
                bool hasOverride = false;
                float overrideVal = 0.0f;
            };
            std::map<std::string, Accum> acc;

            for (const auto& m : mods)
            {
                // Skip modifiers that target attributes the owner does not declare.
                if (!state.has(m.attribute))
                    continue;
                Accum& a = acc[m.attribute];
                switch (m.op)
                {
                case ModifierOp::Add:
                    a.sumAdd += m.magnitude;
                    break;
                case ModifierOp::Multiply:
                    a.prodMul *= m.magnitude;
                    break;
                case ModifierOp::Override:
                    a.hasOverride = true;
                    a.overrideVal = m.magnitude; // last wins (mods are in application order)
                    break;
                }
            }

            auto unclampedOf = [&](const std::string& name, const Attribute& attr) -> float
            {
                auto it = acc.find(name);
                if (it == acc.end())
                    return attr.base; // no mods => base
                const Accum& a = it->second;
                if (a.hasOverride)
                    return a.overrideVal;
                return (attr.base + a.sumAdd) * a.prodMul;
            };

            std::vector<AttributeChange> changes;

            auto writeFinal = [&](const std::string& name, Attribute& attr)
            {
                const float unclamped = unclampedOf(name, attr);
                const auto bounds = resolveAttributeBounds(state, attr);
                const float newCur = clampValue(unclamped, bounds.first, bounds.second);
                const float oldCur = attr.current;
                if (newCur != oldCur)
                {
                    attr.current = newCur;
                    changes.push_back({name, oldCur, newCur});
                }
            };

            // --- Pass 2 (wave A): attributes with STATIC bounds settle first so that ---
            // --- attribute-driven bounds (wave B) read fresh provider currents.       ---
            for (auto& [name, attr] : state.all())
                if (attr.minAttribute.empty() && attr.maxAttribute.empty())
                    writeFinal(name, attr);

            // --- Pass 2 (wave B): attributes whose bounds reference other attributes. ---
            // One level of dependency is resolved deterministically; deeper chains use
            // whatever wave-B order std::map yields (documented limitation).
            for (auto& [name, attr] : state.all())
                if (!attr.minAttribute.empty() || !attr.maxAttribute.empty())
                    writeFinal(name, attr);

            return changes;
        }
    };
}
