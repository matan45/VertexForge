#include <doctest.h>
#include <windows/audio/AudioVoiceRows.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <string>
#include <vector>

// ============================================================
// VK-1515: the active-sounds overlay's ordering and aggregation
// (editor/windows/audio/AudioVoiceRows.hpp).
//
// Worth testing rather than eyeballing for two reasons, both about lying to the reader:
//
//   * The rows arrive in the audio thread's unordered_map order, which is arbitrary and
//     changes between publishes. Any sort that is not a TOTAL order lets the table
//     reshuffle under the cursor at 10Hz, and a debug view that will not hold still is
//     worse than none.
//   * The descending comparator is the classic `ascending ? less : !less` trap — UB that
//     usually looks fine. AssetLifecycleWindow.cpp:189 has it today.
//
// CPU-only: no ImGui context, no audio device. That is why the header is pure.
// ============================================================

namespace
{
    using namespace windows::voicerows;
    using types::AudioVoiceKind;
    using types::AudioVoiceRow;

    AudioVoiceRow row(uint64_t handle, std::string path, std::string bus,
                      AudioVoiceKind kind, float level, uint8_t priority = 128)
    {
        AudioVoiceRow r;
        r.handle = handle;
        r.path = std::move(path);
        r.busName = std::move(bus);
        r.kind = kind;
        r.level = level;
        r.priority = priority;
        return r;
    }

    // Four rows tied on level — the shape that exposes an unstable order.
    std::vector<AudioVoiceRow> tiedRows()
    {
        return {
            row(1, "a/one.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
            row(2, "b/two.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
            row(3, "c/three.vfAudio", "Music", AudioVoiceKind::Stream, 0.5f),
            row(4, "d/four.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
        };
    }

    std::vector<uint64_t> handlesOf(const std::vector<AudioVoiceRow>& rows)
    {
        std::vector<uint64_t> out;
        for (const AudioVoiceRow& r : rows)
            out.push_back(r.handle);
        return out;
    }
}

TEST_SUITE("AudioVoiceRows")
{
    // --- The ordering contract --------------------------------------------------

    TEST_CASE("descending is irreflexive on ties, unlike `ascending ? less : !less`")
    {
        // THE regression. With the shortcut, two rows of equal level each compare "before"
        // the other, which is not a strict weak ordering and is UB in std::sort.
        const AudioVoiceRow a = row(1, "a.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f);
        const AudioVoiceRow b = row(2, "b.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f);

        // Extra parens: doctest decomposes the expression and cannot see through `&&`.
        CHECK_FALSE((rowOrder(a, b, Column::Level, false) && rowOrder(b, a, Column::Level, false)));
        CHECK_FALSE((rowOrder(a, b, Column::Level, true) && rowOrder(b, a, Column::Level, true)));
    }

    TEST_CASE("a row never orders before itself, in either direction")
    {
        const AudioVoiceRow a = row(1, "a.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f);
        for (const Column c : {Column::Sound, Column::Bus, Column::Kind, Column::Level,
                               Column::Priority, Column::Distance, Column::Time})
        {
            CHECK_FALSE(rowOrder(a, a, c, true));
            CHECK_FALSE(rowOrder(a, a, c, false));
        }
    }

    TEST_CASE("the order is total: ties break on handle, the same way in both directions")
    {
        // Direction-independent on purpose. If the tie-break flipped with direction, the
        // rows would still be ordered — just differently — and the table would jump when
        // the user only meant to reverse it.
        const AudioVoiceRow a = row(1, "z.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f);
        const AudioVoiceRow b = row(2, "z.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f);

        CHECK(rowOrder(a, b, Column::Level, true));
        CHECK(rowOrder(a, b, Column::Level, false));
        CHECK_FALSE(rowOrder(b, a, Column::Level, true));
        CHECK_FALSE(rowOrder(b, a, Column::Level, false));
    }

    TEST_CASE("sorting is stable against the publish order it is handed")
    {
        // Rows come from an unordered_map, so this permutation is not hypothetical.
        std::vector<AudioVoiceRow> rows = tiedRows();
        std::vector<AudioVoiceRow> sorted = rows;
        sortRows(sorted, Column::Level, false);
        const std::vector<uint64_t> expected = handlesOf(sorted);

        std::mt19937 rng(1515);
        for (int i = 0; i < 32; ++i)
        {
            std::shuffle(rows.begin(), rows.end(), rng);
            std::vector<AudioVoiceRow> attempt = rows;
            sortRows(attempt, Column::Level, false);
            CHECK(handlesOf(attempt) == expected);
        }
    }

    // --- Per-column behaviour ---------------------------------------------------

    TEST_CASE("level descending is the default view: loudest first")
    {
        std::vector<AudioVoiceRow> rows{
            row(1, "quiet.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.1f),
            row(2, "loud.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.9f),
            row(3, "mid.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
        };
        sortRows(rows, Column::Level, false);
        CHECK(handlesOf(rows) == std::vector<uint64_t>{2, 3, 1});
    }

    TEST_CASE("priority ascending puts 0 first, because lower is more important")
    {
        // Pins the direction against being read backwards — the one thing about this column
        // that is guaranteed to trip someone up (VoicePolicy: 0 = critical, 255 = least).
        std::vector<AudioVoiceRow> rows{
            row(1, "normal.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f, 128),
            row(2, "critical.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f, 0),
            row(3, "low.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f, 255),
        };
        sortRows(rows, Column::Priority, true);
        CHECK(handlesOf(rows) == std::vector<uint64_t>{2, 1, 3});
    }

    TEST_CASE("the Sound column sorts on the file name shown, not the hidden path")
    {
        // Sorting on the full path would order by folder while the eye reads file names —
        // the list would look randomly ordered.
        std::vector<AudioVoiceRow> rows{
            row(1, "zzz/aaa.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
            row(2, "aaa/zzz.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
        };
        sortRows(rows, Column::Sound, true);
        CHECK(handlesOf(rows) == std::vector<uint64_t>{1, 2}); // aaa.vfAudio before zzz.vfAudio
    }

    TEST_CASE("a NaN level never breaks the ordering")
    {
        // Levels are sanitized at publish, so this is defence in depth — but a NaN in a
        // comparator is UB, and the cost of pinning it is one test.
        std::vector<AudioVoiceRow> rows{
            row(1, "nan.vfAudio", "SFX", AudioVoiceKind::Sound2D,
                std::numeric_limits<float>::quiet_NaN()),
            row(2, "good.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
            row(3, "also.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.1f),
        };
        sortRows(rows, Column::Level, false);
        CHECK(rows.size() == 3); // did not crash, and lost nothing
    }

    // --- displayFileName --------------------------------------------------------

    TEST_CASE("displayFileName strips both separator flavours and copes with neither")
    {
        // Compared as std::string: doctest stringifies operands, and string_view has no
        // ostream operator unless <ostream> happens to be included first.
        CHECK(std::string(displayFileName("assets/sfx/boom.vfAudio")) == "boom.vfAudio");
        CHECK(std::string(displayFileName("assets\\sfx\\boom.vfAudio")) == "boom.vfAudio");
        CHECK(std::string(displayFileName("boom.vfAudio")) == "boom.vfAudio");
        CHECK(std::string(displayFileName("")).empty());
        CHECK(std::string(displayFileName("assets/sfx/")).empty()); // trailing separator
    }

    // --- Aggregation ------------------------------------------------------------

    TEST_CASE("countByBus totals per bus, busiest first")
    {
        const std::vector<AudioVoiceRow> rows{
            row(1, "a.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
            row(2, "b.vfAudio", "Music", AudioVoiceKind::Stream, 0.5f),
            row(3, "c.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
            row(4, "d.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
        };
        const auto counts = countByBus(rows);
        REQUIRE(counts.size() == 2);
        CHECK(counts[0].busName == "SFX");
        CHECK(counts[0].voices == 3);
        CHECK(counts[1].busName == "Music");
        CHECK(counts[1].voices == 1);
    }

    TEST_CASE("countByBus breaks equal counts on name so the list holds still")
    {
        const std::vector<AudioVoiceRow> rows{
            row(1, "a.vfAudio", "Zebra", AudioVoiceKind::Sound2D, 0.5f),
            row(2, "b.vfAudio", "Alpha", AudioVoiceKind::Sound2D, 0.5f),
        };
        const auto counts = countByBus(rows);
        REQUIRE(counts.size() == 2);
        CHECK(counts[0].busName == "Alpha");
        CHECK(counts[1].busName == "Zebra");
    }

    TEST_CASE("countByBus of nothing is nothing")
    {
        CHECK(countByBus({}).empty());
    }

    TEST_CASE("only pooled voices count against the budget")
    {
        // Streaming is exempt and virtual voices hold no source — counting either against
        // the cap would misreport the readout the whole budget is verified with.
        const std::vector<AudioVoiceRow> rows{
            row(1, "a.vfAudio", "SFX", AudioVoiceKind::Sound3D, 0.5f),
            row(2, "b.vfAudio", "SFX", AudioVoiceKind::Sound2D, 0.5f),
            row(3, "c.vfAudio", "Music", AudioVoiceKind::Stream, 0.5f),
            row(4, "d.vfAudio", "SFX", AudioVoiceKind::Virtual, 0.5f),
            row(5, "e.vfAudio", "SFX", AudioVoiceKind::Virtual, 0.5f),
        };
        CHECK(countRealVoices(rows) == 2);
        CHECK(countKind(rows, AudioVoiceKind::Stream) == 1);
        CHECK(countKind(rows, AudioVoiceKind::Virtual) == 2);
        CHECK(countKind(rows, AudioVoiceKind::Sound3D) == 1);
    }

    // --- Labels -----------------------------------------------------------------

    TEST_CASE("priority labels name the convention rather than restating the number")
    {
        CHECK(std::string(priorityLabel(0)) == "Critical");
        CHECK(std::string(priorityLabel(128)) == "Normal");
        CHECK(std::string(priorityLabel(255)) == "Low");
        CHECK(std::string(priorityLabel(32)) == "High");
        CHECK(std::string(priorityLabel(200)) == "Low");
    }

    TEST_CASE("voice kinds have display names")
    {
        CHECK(std::string(types::audioVoiceKindToString(AudioVoiceKind::Sound2D)) == "2D");
        CHECK(std::string(types::audioVoiceKindToString(AudioVoiceKind::Sound3D)) == "3D");
        CHECK(std::string(types::audioVoiceKindToString(AudioVoiceKind::Stream)) == "Stream");
        CHECK(std::string(types::audioVoiceKindToString(AudioVoiceKind::Virtual)) == "Virtual");
    }
}
