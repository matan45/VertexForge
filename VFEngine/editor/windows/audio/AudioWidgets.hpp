#pragma once

// VK-1515: audio UI pieces shared by more than one window.
//
// All of this began life in an anonymous namespace inside AudioMixerWindow.cpp (VK-1514's
// meters) and in AudioConfigWindow.cpp (VK-1513's voice readout). Anonymous means internal
// linkage, so AudioDebugWindow could not reuse a line of it — and the alternative, copying,
// would have put the -60 dB floor and the 0.8/0.95 colour thresholds in three places that
// drift apart the first time anyone retunes one of them.
//
// What is shared here is the SEMANTICS — the dB mapping, the thresholds, the colour ramp,
// the "N / M" readout. Geometry is deliberately NOT: the mixer's meter is a vertical
// 12x150 strip beside a fader, the overlay's is a short horizontal bar in a table cell.
// Those are genuinely different and pretending otherwise would need a parameter for every
// difference.
//
// Header-only and imgui-only: this is presentation, so it must not reach for the dispatcher
// or anything under core/.

#include "types/AudioTypes.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace windows::audiowidgets
{
    // Everything quieter than this reads as silence. Audio levels are logarithmic and the
    // interesting range is the top ~60 dB; a linear bar spends 90% of its length on sounds
    // nobody can hear.
    inline constexpr float kMeterFloorDb = -60.0f;

    // Amplitude (0..1) -> bar fill (0..1), through dB. Non-finite and <= 0 land on empty
    // rather than -inf, which is why this exists instead of an inline log10 at each site.
    inline float meterNorm(float value)
    {
        if (!std::isfinite(value) || value <= 0.0f)
            return 0.0f;
        const float db = 20.0f * std::log10(value);
        return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
    }

    // Green until it is loud, amber approaching the top, red at it. Takes the NORMALISED
    // value, not the amplitude, so the thresholds mean the same thing on every meter.
    inline ImU32 meterColour(float normalized)
    {
        if (normalized < 0.8f)
            return IM_COL32(90, 200, 90, 255);
        if (normalized < 0.95f)
            return IM_COL32(220, 200, 60, 255);
        return IM_COL32(230, 80, 60, 255);
    }

    inline constexpr ImU32 kMeterBackground = IM_COL32(40, 40, 40, 255);
    inline constexpr ImU32 kMeterPeakLine = IM_COL32(240, 240, 240, 255);

    // Vertical bar with a peak-hold tick. The mixer's strip meter (VK-1514).
    inline void drawVerticalMeter(ImVec2 pos, ImVec2 size, float rms, float peakHold)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kMeterBackground);

        const float normalizedRms = meterNorm(rms);
        const float fillHeight = normalizedRms * size.y;
        if (fillHeight > 0.0f)
        {
            drawList->AddRectFilled(ImVec2(pos.x, pos.y + size.y - fillHeight),
                                    ImVec2(pos.x + size.x, pos.y + size.y),
                                    meterColour(normalizedRms));
        }

        const float normalizedPeak = meterNorm(peakHold);
        if (normalizedPeak > 0.0f)
        {
            const float y = pos.y + size.y - normalizedPeak * size.y;
            drawList->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), kMeterPeakLine, 2.0f);
        }
    }

    // Horizontal bar, for a table cell. No peak-hold: a per-voice level is already an
    // estimate sampled at 10Hz, so a hold on top of it would imply a precision it has not
    // got. `dimmed` is for paused voices, which still hold their slot but make no sound.
    inline void drawLevelBar(ImVec2 pos, ImVec2 size, float level, bool dimmed)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kMeterBackground);

        const float normalized = meterNorm(level);
        const float fillWidth = normalized * size.x;
        if (fillWidth > 0.0f)
        {
            ImU32 colour = meterColour(normalized);
            if (dimmed)
            {
                // Halve the alpha rather than the RGB: the colour still carries the level,
                // it is just visibly not contributing right now.
                colour = (colour & 0x00FFFFFF) | (ImU32(110) << IM_COL32_A_SHIFT);
            }
            drawList->AddRectFilled(pos, ImVec2(pos.x + fillWidth, pos.y + size.y), colour);
        }
    }

    // "Real voices: N / M", coloured by how close the budget is to binding (VK-1513).
    // Amber as it fills, red once it binds and sounds start being stolen or virtualized —
    // that transition is the thing worth noticing, and it is the instrument the whole voice
    // budget is verified with.
    inline void drawVoiceCountReadout(const types::AudioVoiceStats& stats)
    {
        ImGui::Text("Real voices:");
        ImGui::SameLine();
        if (stats.maxRealVoices <= 0)
        {
            ImGui::TextDisabled("%d / unlimited (cap disabled)", stats.realVoices);
        }
        else
        {
            const float load = static_cast<float>(stats.realVoices) /
                               static_cast<float>(stats.maxRealVoices);
            ImVec4 colour(0.3f, 1.0f, 0.3f, 1.0f);
            if (load >= 1.0f)
                colour = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (load >= 0.8f)
                colour = ImVec4(1.0f, 0.63f, 0.0f, 1.0f);
            ImGui::TextColored(colour, "%d / %d", stats.realVoices, stats.maxRealVoices);
        }

        // VK-1515: only meaningful once virtualization exists — before it, a voice that lost
        // the budget was simply gone. Shown unconditionally so that "0 virtual" is itself
        // information: the budget is not binding.
        ImGui::SameLine();
        ImGui::TextDisabled("| virtual: %d", stats.virtualVoices);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Voices that lost the budget and are being kept on a simulated\n"
                              "playback clock. They hold no source and cost no mixing — they\n"
                              "are revived at the right offset if they become audible again.");
        }
    }
}
