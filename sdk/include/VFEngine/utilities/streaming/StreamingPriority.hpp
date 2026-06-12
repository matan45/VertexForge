#pragma once

namespace streaming
{
    // Weighted-term priority accumulator — the shape every streaming system uses
    // (lights, GPU objects, terrain tiles): inverse-distance decay plus weighted
    // bonuses. Build once per entry per update.
    class WeightedPriority
    {
    public:
        WeightedPriority& add(float value, float weight)
        {
            total += value * weight;
            return *this;
        }

        // Inverse-distance decay: weight / (1 + distance * falloff)
        WeightedPriority& addInverseDistance(float distance, float weight,
                                             float falloff = 0.01f)
        {
            total += weight / (1.0f + distance * falloff);
            return *this;
        }

        WeightedPriority& addIf(bool condition, float bonus)
        {
            if (condition)
                total += bonus;
            return *this;
        }

        [[nodiscard]] float value() const { return total; }

    private:
        float total = 0.0f;
    };

    // Radius-pair hysteresis (SectorStreamer / TerrainWorldStreamer shape):
    // activate when the value drops to enterThreshold, deactivate only once it
    // exceeds the larger exitThreshold — the band between them prevents thrash.
    struct HysteresisBand
    {
        float enterThreshold = 0.0f;
        float exitThreshold = 0.0f;

        HysteresisBand() = default;
        HysteresisBand(float enter, float exit)
            : enterThreshold(enter), exitThreshold(exit < enter ? enter : exit) {}

        [[nodiscard]] bool shouldActivate(float value) const
        {
            return value <= enterThreshold;
        }

        [[nodiscard]] bool shouldDeactivate(float value) const
        {
            return value > exitThreshold;
        }

        // True while the value sits between the thresholds: keep current state
        [[nodiscard]] bool inBand(float value) const
        {
            return value > enterThreshold && value <= exitThreshold;
        }
    };

    // Margin-based priority hysteresis (LightStreamManager shape): an active
    // entry whose priority is within `margin` of the activation cutoff keeps its
    // slot, so entries hovering at the cutoff don't pop in and out per frame.
    struct PriorityHysteresis
    {
        float margin = 0.0f;

        [[nodiscard]] bool shouldKeepActive(float priority, float cutoffPriority) const
        {
            return priority >= cutoffPriority - margin;
        }
    };

} // namespace streaming
