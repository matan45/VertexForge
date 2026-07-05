#pragma once

#include <cstdint>
#include <bit>

// ============================================================================
// Virtual Texturing (VK-1480) — 1-bit-per-entry packed feedback bitmap helpers.
// PURE / header-only (Tests project validates via test_vt_feedback_words) and the
// exact CPU mirror of the GLSL write in vt_sampling.glsl::vtWriteFeedback:
//
//     atomicOr(VT_FEEDBACK[entryIdx >> 5u], 1u << (entryIdx & 31u));
//
// Packing feedback 32x smaller (one uint per 32 page-table entries instead of one
// uint per entry) shrinks the every-frame fill/copy/readback/scan from megabytes
// to kilobytes; a page request is idempotent, so a shared bit loses nothing.
// ============================================================================

namespace render::vt
{
    inline constexpr uint32_t VT_FEEDBACK_WORD_BITS = 32u;

    // Words needed to hold one bit per page-table entry.
    inline uint32_t vtFeedbackWordCount(uint32_t totalEntries)
    {
        return (totalEntries + VT_FEEDBACK_WORD_BITS - 1u) / VT_FEEDBACK_WORD_BITS;
    }

    inline uint32_t vtFeedbackWordIndex(uint32_t entryIdx) { return entryIdx >> 5u; }

    inline uint32_t vtFeedbackBitMask(uint32_t entryIdx) { return 1u << (entryIdx & 31u); }

    // Visit every set entry index in ascending order, skipping zero words (the
    // common case — most of the bitmap is untouched on any given frame). Bits at
    // or beyond totalEntries in the final word are ignored.
    template <typename Fn>
    inline void vtForEachSetEntry(const uint32_t* words, uint32_t wordCount, uint32_t totalEntries, Fn&& fn)
    {
        for (uint32_t w = 0; w < wordCount; ++w)
        {
            uint32_t bits = words[w];
            while (bits != 0u)
            {
                const uint32_t b = static_cast<uint32_t>(std::countr_zero(bits));
                bits &= bits - 1u; // clear lowest set bit
                const uint32_t entry = w * VT_FEEDBACK_WORD_BITS + b;
                if (entry >= totalEntries)
                    return; // tail padding bits in the last word
                fn(entry);
            }
        }
    }
}
