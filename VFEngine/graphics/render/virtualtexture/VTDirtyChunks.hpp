#pragma once

#include <cstdint>
#include <vector>

// ============================================================================
// Virtual Texturing (VK-1480) — chunked dirty tracking for the page-table upload.
// PURE / header-only (Tests project validates via test_vt_dirty_chunks).
//
// VTPageTable previously re-uploaded its whole used range whenever ANY entry
// changed; for RVT the used range is the entire mip pyramid (up to ~1.4M entries
// = 5.6 MB) and page churn happens on nearly every camera-motion frame. Tracking
// dirtiness in fixed 4096-entry (16 KB) chunks and uploading only the coalesced
// dirty ranges bounds a typical churn frame to a few chunks.
//
// A single min/max dirty range was rejected: the mip-0 block alone can span ~1M
// entries, so two pages at opposite corners would degenerate to a full re-upload.
// ============================================================================

namespace render::vt
{
    class VTDirtyChunks
    {
    public:
        static constexpr uint32_t CHUNK_ENTRIES = 4096u; // 16 KB of uint32 entries per chunk

        // Configure for a table of totalEntries; everything starts clean.
        void reset(uint32_t totalEntriesIn)
        {
            totalEntries = totalEntriesIn;
            chunkDirty.assign(chunkCount(), 0u);
            anyDirty = false;
        }

        void markEntry(uint32_t entryIdx)
        {
            if (entryIdx >= totalEntries)
                return;
            chunkDirty[entryIdx / CHUNK_ENTRIES] = 1u;
            anyDirty = true;
        }

        void markRange(uint32_t firstEntry, uint32_t count)
        {
            if (count == 0u || firstEntry >= totalEntries)
                return;
            const uint32_t lastEntry = (firstEntry + count - 1u < totalEntries)
                                           ? firstEntry + count - 1u
                                           : totalEntries - 1u;
            const uint32_t firstChunk = firstEntry / CHUNK_ENTRIES;
            const uint32_t lastChunk = lastEntry / CHUNK_ENTRIES;
            for (uint32_t c = firstChunk; c <= lastChunk; ++c)
                chunkDirty[c] = 1u;
            anyDirty = true;
        }

        void markAll()
        {
            if (totalEntries == 0u)
                return;
            chunkDirty.assign(chunkCount(), 1u);
            anyDirty = true;
        }

        [[nodiscard]] bool any() const { return anyDirty; }

        [[nodiscard]] uint32_t chunkCount() const
        {
            return totalEntries == 0u ? 0u : (totalEntries + CHUNK_ENTRIES - 1u) / CHUNK_ENTRIES;
        }

        struct Range
        {
            uint32_t firstEntry = 0;
            uint32_t entryCount = 0;
        };

        // Coalesce adjacent dirty chunks into entry ranges (the final range is
        // clamped to totalEntries) and clear all dirty state. Appends to `out`
        // after clearing it; empty when nothing was dirty.
        void takeRanges(std::vector<Range>& out)
        {
            out.clear();
            if (!anyDirty)
                return;
            const uint32_t chunks = chunkCount();
            for (uint32_t c = 0; c < chunks;)
            {
                if (chunkDirty[c] == 0u)
                {
                    ++c;
                    continue;
                }
                uint32_t end = c;
                while (end + 1u < chunks && chunkDirty[end + 1u] != 0u)
                    ++end;
                const uint32_t firstEntry = c * CHUNK_ENTRIES;
                const uint32_t lastEntryExcl = ((end + 1u) * CHUNK_ENTRIES < totalEntries)
                                                   ? (end + 1u) * CHUNK_ENTRIES
                                                   : totalEntries;
                out.push_back({firstEntry, lastEntryExcl - firstEntry});
                c = end + 1u;
            }
            chunkDirty.assign(chunks, 0u);
            anyDirty = false;
        }

    private:
        std::vector<uint8_t> chunkDirty;
        uint32_t totalEntries = 0;
        bool anyDirty = false;
    };
}
