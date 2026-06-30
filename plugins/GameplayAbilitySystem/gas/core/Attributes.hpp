#pragma once

// Gameplay Ability System (VK-816) — owner attributes + modifier types.
// ENGINE-FREE (see Tags.hpp header note).

#include <map>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <utility>
#include <cstdint>

namespace gas
{
    // How a modifier combines into an attribute's value.
    //   Add      -> contributes to sum(Add)
    //   Multiply -> contributes a direct factor to product(Multiply); magnitude
    //               1.5 means x1.5 (a +50% buff). There is NO implicit +1.
    //   Override -> sets the value outright (last-applied override wins).
    enum class ModifierOp
    {
        Add,
        Multiply,
        Override
    };

    // One attribute modifier. `sourceHandle` is runtime bookkeeping (which ActiveEffect
    // produced it); it is not persisted in assets and defaults to 0.
    struct AttributeModifier
    {
        std::string attribute;
        ModifierOp op = ModifierOp::Add;
        float magnitude = 0.0f;
        std::uint64_t sourceHandle = 0;

        bool operator==(const AttributeModifier& o) const
        {
            return attribute == o.attribute && op == o.op &&
                   magnitude == o.magnitude && sourceHandle == o.sourceHandle;
        }
        bool operator!=(const AttributeModifier& o) const { return !(*this == o); }
    };

    // Reported when recompute changes an attribute's current value.
    struct AttributeChange
    {
        std::string attribute;
        float oldValue = 0.0f;
        float newValue = 0.0f;
    };

    // One owner attribute. `current` is written ONLY by the Aggregator (the
    // setters below seed it from the static clamp(base) for sanity before any
    // recompute). min/max may optionally be driven by another attribute's current
    // value via minAttribute / maxAttribute (e.g. Health.max = MaxHealth.current).
    struct Attribute
    {
        float base = 0.0f;
        float minValue = -std::numeric_limits<float>::infinity();
        float maxValue = std::numeric_limits<float>::infinity();
        std::string minAttribute; // optional: min := current of this attribute
        std::string maxAttribute; // optional: max := current of this attribute
        float current = 0.0f;

        bool operator==(const Attribute& o) const
        {
            return base == o.base && minValue == o.minValue && maxValue == o.maxValue &&
                   minAttribute == o.minAttribute && maxAttribute == o.maxAttribute &&
                   current == o.current;
        }
        bool operator!=(const Attribute& o) const { return !(*this == o); }
    };

    inline float clampValue(float v, float lo, float hi)
    {
        if (hi < lo)
            hi = lo;
        return std::clamp(v, lo, hi);
    }

    // Per-owner attribute store. std::map keeps deterministic (sorted) iteration.
    class AttributeState
    {
    public:
        bool has(const std::string& name) const
        {
            return m_attrs.find(name) != m_attrs.end();
        }

        // Creates the attribute with defaults if absent, seeding current = clamp(base).
        Attribute& ensure(const std::string& name)
        {
            auto it = m_attrs.find(name);
            if (it == m_attrs.end())
            {
                Attribute a;
                a.current = clampValue(a.base, a.minValue, a.maxValue);
                it = m_attrs.emplace(name, a).first;
            }
            return it->second;
        }

        // Stores an attribute and seeds current = clamp(base, static bounds).
        void setAttribute(const std::string& name, Attribute attr)
        {
            attr.current = clampValue(attr.base, attr.minValue, attr.maxValue);
            m_attrs[name] = attr;
        }

        void setBase(const std::string& name, float value)
        {
            Attribute& a = ensure(name);
            a.base = value;
            a.current = clampValue(a.base, a.minValue, a.maxValue);
        }

        // Returns a reference; caller must ensure the attribute exists.
        Attribute& get(const std::string& name) { return m_attrs.at(name); }
        const Attribute& get(const std::string& name) const { return m_attrs.at(name); }

        const Attribute* find(const std::string& name) const
        {
            auto it = m_attrs.find(name);
            return it == m_attrs.end() ? nullptr : &it->second;
        }

        float currentOf(const std::string& name) const
        {
            auto it = m_attrs.find(name);
            return it == m_attrs.end() ? 0.0f : it->second.current;
        }

        float baseOf(const std::string& name) const
        {
            auto it = m_attrs.find(name);
            return it == m_attrs.end() ? 0.0f : it->second.base;
        }

        std::map<std::string, Attribute>& all() { return m_attrs; }
        const std::map<std::string, Attribute>& all() const { return m_attrs; }

        std::map<std::string, Attribute>::iterator begin() { return m_attrs.begin(); }
        std::map<std::string, Attribute>::iterator end() { return m_attrs.end(); }
        std::map<std::string, Attribute>::const_iterator begin() const { return m_attrs.begin(); }
        std::map<std::string, Attribute>::const_iterator end() const { return m_attrs.end(); }

        bool operator==(const AttributeState& o) const { return m_attrs == o.m_attrs; }
        bool operator!=(const AttributeState& o) const { return !(*this == o); }

    private:
        std::map<std::string, Attribute> m_attrs;
    };

    // Resolves the effective [min, max] bounds for an attribute, honoring
    // attribute-driven bounds (which read the CURRENT value of the referenced
    // attribute). Guards hi >= lo.
    inline std::pair<float, float> resolveAttributeBounds(const AttributeState& state, const Attribute& a)
    {
        float lo = a.minValue;
        float hi = a.maxValue;
        if (!a.minAttribute.empty() && state.has(a.minAttribute))
            lo = state.get(a.minAttribute).current;
        if (!a.maxAttribute.empty() && state.has(a.maxAttribute))
            hi = state.get(a.maxAttribute).current;
        if (hi < lo)
            hi = lo;
        return {lo, hi};
    }
}
