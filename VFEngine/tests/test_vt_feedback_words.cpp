#include <doctest.h>
#include <render/virtualtexture/VTFeedbackWords.hpp>

#include <bit>
#include <vector>

// ============================================================================
// VT bit-packed feedback bitmap helpers (VK-1480). Pure, CPU-only. This is the
// C++ side of the parity contract with the GLSL write in
// resources/shaders/common/vt_sampling.glsl::vtWriteFeedback:
//     atomicOr(VT_FEEDBACK[entryIdx >> 5u], 1u << (entryIdx & 31u));
// ============================================================================

using namespace render::vt;

namespace
{
    void setBit(std::vector<uint32_t>& words, uint32_t entry)
    {
        words[vtFeedbackWordIndex(entry)] |= vtFeedbackBitMask(entry);
    }

    std::vector<uint32_t> collect(const std::vector<uint32_t>& words, uint32_t totalEntries)
    {
        std::vector<uint32_t> visited;
        vtForEachSetEntry(words.data(), static_cast<uint32_t>(words.size()), totalEntries,
                          [&](uint32_t e) { visited.push_back(e); });
        return visited;
    }
}

TEST_CASE("VT feedback words: word count edges")
{
    CHECK(vtFeedbackWordCount(0) == 0u);
    CHECK(vtFeedbackWordCount(1) == 1u);
    CHECK(vtFeedbackWordCount(31) == 1u);
    CHECK(vtFeedbackWordCount(32) == 1u);
    CHECK(vtFeedbackWordCount(33) == 2u);
    CHECK(vtFeedbackWordCount(64) == 2u);
    CHECK(vtFeedbackWordCount(65) == 3u);
    // ~1.4M RVT entries: exact ceil.
    CHECK(vtFeedbackWordCount(1'400'000) == (1'400'000u + 31u) / 32u);
}

TEST_CASE("VT feedback words: word-index / bit-mask round trip")
{
    const uint32_t entries[] = {0u, 1u, 31u, 32u, 33u, 63u, 64u, 1000u, 1'400'000u};
    for (uint32_t e : entries)
    {
        const uint32_t w = vtFeedbackWordIndex(e);
        const uint32_t mask = vtFeedbackBitMask(e);
        CHECK(w == e / 32u);
        // Exactly one bit set, at position e % 32.
        CHECK(mask == (1u << (e & 31u)));
        // Reconstruct the entry from (word, bit).
        const uint32_t bit = static_cast<uint32_t>(std::countr_zero(mask));
        CHECK(w * 32u + bit == e);
    }
}

TEST_CASE("VT feedback words: empty bitmap visits nothing")
{
    std::vector<uint32_t> words(vtFeedbackWordCount(100), 0u);
    const auto visited = collect(words, 100u);
    CHECK(visited.empty());
}

TEST_CASE("VT feedback words: visits set entries ascending, skips zero words")
{
    const uint32_t totalEntries = 200u; // 7 words
    std::vector<uint32_t> words(vtFeedbackWordCount(totalEntries), 0u);

    // Deliberately set out of order across non-adjacent words (words 2,4,5 stay 0).
    const uint32_t set[] = {160u, 3u, 96u, 31u, 97u};
    for (uint32_t e : set)
        setBit(words, e);

    const auto visited = collect(words, totalEntries);
    // Ascending, exactly the set entries, no duplicates.
    const std::vector<uint32_t> expected = {3u, 31u, 96u, 97u, 160u};
    CHECK(visited == expected);
}

TEST_CASE("VT feedback words: tail padding bits beyond totalEntries are ignored")
{
    const uint32_t totalEntries = 33u; // wordCount 2; word 1 holds entries 32..63
    std::vector<uint32_t> words(vtFeedbackWordCount(totalEntries), 0u);
    CHECK(words.size() == 2u);

    setBit(words, 3u);   // valid
    setBit(words, 31u);  // valid (last bit of word 0)
    setBit(words, 32u);  // valid (only in-range bit of word 1)
    // Padding bits >= totalEntries in the final word: must never be visited.
    words[1] |= (1u << 1u); // entry 33
    words[1] |= (1u << 5u); // entry 37
    words[1] |= (1u << 31u); // entry 63

    const auto visited = collect(words, totalEntries);
    const std::vector<uint32_t> expected = {3u, 31u, 32u};
    CHECK(visited == expected);
}

TEST_CASE("VT feedback words: dense all-set matches brute force")
{
    const uint32_t totalEntries = 100u; // not a multiple of 32
    std::vector<uint32_t> words(vtFeedbackWordCount(totalEntries), 0xFFFFFFFFu);

    const auto visited = collect(words, totalEntries);

    std::vector<uint32_t> expected;
    expected.reserve(totalEntries);
    for (uint32_t e = 0; e < totalEntries; ++e)
        expected.push_back(e);

    CHECK(visited.size() == totalEntries);
    CHECK(visited == expected);
}
