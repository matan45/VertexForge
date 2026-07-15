#pragma once

// VK-1515: the non-UI half of the active-sounds overlay — ordering, aggregation and
// formatting of voice rows.
//
// Split out from AudioDebugWindow.cpp so it can be tested without an ImGui context or an
// audio device, in the same spirit as core/audio/VoicePolicy.hpp and BusMetering.hpp: the
// decisions are pure functions over plain data, and only the drawing needs a frame.
// Precedent for a doctested editor header: windows/contentbrowser/AssetQueryParser.hpp and
// windows/viewport/GroupTransformMath.hpp.
//
// Header-only and imgui-free — Tests links neither imgui nor the Audio DLL.

#include "types/AudioTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace windows::voicerows
{
    // Column order here IS the table's column order — the sort spec arrives from ImGui as a
    // column index, and this is what it indexes into.
    enum class Column : int
    {
        Sound = 0,
        Bus,
        Kind,
        Level,
        Priority,
        Distance,
        Time
    };

    struct BusCount
    {
        std::string busName;
        int voices = 0;
    };

    // "assets/sfx/explosion_large.vfAudio" -> "explosion_large.vfAudio". The table shows the
    // file, the tooltip shows the path: at a glance you are identifying which sound, not
    // where it lives, and full paths make every row the same width of noise.
    inline std::string_view displayFileName(std::string_view path)
    {
        const std::size_t slash = path.find_last_of("/\\");
        return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    // Strictly "is a before b on this column", ignoring direction. Never a tie-break — that
    // belongs to rowOrder, which has to apply it independently of direction.
    inline bool rowLess(const types::AudioVoiceRow& a, const types::AudioVoiceRow& b, Column column)
    {
        switch (column)
        {
        case Column::Bus:
            return a.busName < b.busName;
        case Column::Kind:
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        case Column::Level:
            return a.level < b.level;
        case Column::Priority:
            // Numeric order. 0 = most important, so ascending puts the critical sounds on
            // top, which is what the column header promises.
            return a.priority < b.priority;
        case Column::Distance:
            return a.distance < b.distance;
        case Column::Time:
            return a.playbackPosition < b.playbackPosition;
        case Column::Sound:
            break;
        }
        return displayFileName(a.path) < displayFileName(b.path);
    }

    // A TOTAL order over rows: no two distinct rows ever compare equivalent, because handles
    // are unique.
    //
    // Descending is an explicit inverse comparison rather than `ascending ? less : !less`.
    // That shortcut is not a strict weak ordering — for equivalent elements !less is true in
    // both directions, which std::sort is entitled to turn into a crash.
    // (windows/asset/AssetLifecycleWindow.cpp has exactly that bug and gets away with it
    // only because stable_sort happens to be forgiving.)
    //
    // The handle tie-break is deliberately direction-INDEPENDENT: rows are published in the
    // audio thread's unordered_map order, which is arbitrary and unstable, so without a
    // final total tie-break a table of equal-level voices would visibly reshuffle on every
    // 10Hz publish. Same rule as VoicePolicy::moreImportant, so the overlay and the voice
    // budget never disagree about which of two tied voices comes first.
    inline bool rowOrder(const types::AudioVoiceRow& a, const types::AudioVoiceRow& b,
                         Column column, bool ascending)
    {
        if (rowLess(a, b, column)) return ascending;
        if (rowLess(b, a, column)) return !ascending;
        return a.handle < b.handle;
    }

    inline void sortRows(std::vector<types::AudioVoiceRow>& rows, Column column, bool ascending)
    {
        std::sort(rows.begin(), rows.end(),
                  [column, ascending](const types::AudioVoiceRow& a, const types::AudioVoiceRow& b)
                  {
                      return rowOrder(a, b, column, ascending);
                  });
    }

    // How many voices each bus is carrying, busiest first. Answers "what is eating the
    // budget" without reading every row. Ties break on name so the list does not reorder
    // itself between publishes.
    inline std::vector<BusCount> countByBus(std::span<const types::AudioVoiceRow> rows)
    {
        std::vector<BusCount> counts;
        for (const types::AudioVoiceRow& row : rows)
        {
            const auto it = std::find_if(counts.begin(), counts.end(),
                                         [&row](const BusCount& c) { return c.busName == row.busName; });
            if (it != counts.end())
                ++it->voices;
            else
                counts.push_back(BusCount{row.busName, 1});
        }

        std::sort(counts.begin(), counts.end(), [](const BusCount& a, const BusCount& b)
        {
            if (a.voices != b.voices) return a.voices > b.voices;
            return a.busName < b.busName;
        });
        return counts;
    }

    // Voices actually occupying a pool slot. Streaming is exempt from the budget and virtual
    // voices hold no source, so neither belongs in a "vs cap" figure.
    inline int countRealVoices(std::span<const types::AudioVoiceRow> rows)
    {
        return static_cast<int>(std::count_if(rows.begin(), rows.end(),
            [](const types::AudioVoiceRow& row)
            {
                return row.kind == types::AudioVoiceKind::Sound2D
                    || row.kind == types::AudioVoiceKind::Sound3D;
            }));
    }

    inline int countKind(std::span<const types::AudioVoiceRow> rows, types::AudioVoiceKind kind)
    {
        return static_cast<int>(std::count_if(rows.begin(), rows.end(),
            [kind](const types::AudioVoiceRow& row) { return row.kind == kind; }));
    }

    // 128 -> "128 (Normal)". The number alone reads backwards to anyone who has not just
    // read VoicePolicy — lower is more important — so the label carries the meaning.
    inline const char* priorityLabel(uint8_t priority)
    {
        if (priority == 0) return "Critical";
        if (priority < 64) return "High";
        if (priority < 128) return "Above normal";
        if (priority == 128) return "Normal";
        if (priority < 192) return "Below normal";
        return "Low";
    }
}
