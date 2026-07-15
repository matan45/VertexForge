#pragma once

// Pure effective-gain policy shared by AudioBusManager and CPU-only tests.
// Mute is an explicit routing decision and therefore always wins over solo.

namespace core::audio::gainpolicy
{
    [[nodiscard]] constexpr float rootEffectiveVolume(float volume, bool muted,
                                                       bool soloed, bool anySoloed) noexcept
    {
        if (muted)
        {
            return 0.0f;
        }
        if (anySoloed && !soloed)
        {
            return 0.0f;
        }
        return volume;
    }

    [[nodiscard]] constexpr float childEffectiveVolume(float volume, float parentEffective,
                                                        bool muted, bool parentMuted,
                                                        bool soloed, bool anySoloed) noexcept
    {
        if (muted || parentMuted)
        {
            return 0.0f;
        }
        if (!anySoloed)
        {
            return volume * parentEffective;
        }
        if (!soloed)
        {
            return 0.0f;
        }

        // Preserve the existing solo-orphan rule: a soloed child may bypass a parent
        // suppressed only by solo filtering. The mute checks above prevent resurrection
        // through an explicitly muted ancestor.
        return parentEffective == 0.0f ? volume : volume * parentEffective;
    }
}
