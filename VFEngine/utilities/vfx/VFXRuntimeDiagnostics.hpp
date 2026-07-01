#pragma once

// VK-1453 (VFXSequence Phase 4) — actionable, non-spammy runtime warnings (AC6).
//
// A process-wide, deduplicating VFX warning collector. report() records a
// (source,message) pair and returns true only the first time it is seen, so the
// caller logs it once (via vfLogWarning) and repeats merely bump a count instead of
// spamming the log. A bounded ring of the most-recent distinct warnings is exposed to
// the VFX debug UI. Kept free of print/Log.hpp (which pulls Windows console APIs) so
// it stays cleanly includable from the Services-only Tests project. Services is
// static-linked into the Editor/Runtime EXE, so this singleton is one instance per
// process.

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace vfx
{
    struct VFXWarning
    {
        std::string source;
        std::string message;
        uint32_t count = 0;    // total times this (source,message) was reported
        uint64_t lastSeq = 0;  // monotonic sequence of the most recent report (UI recency sort)
    };

    class VFXRuntimeDiagnostics
    {
    public:
        static VFXRuntimeDiagnostics& instance()
        {
            static VFXRuntimeDiagnostics inst;
            return inst;
        }

        // Record a warning. Returns true if this (source,message) is newly seen in
        // the ring (the caller should log it once); repeats only increment the count.
        bool report(std::string_view source, std::string_view message)
        {
            std::lock_guard<std::mutex> lock(mutex);
            const uint64_t seq = ++seqCounter;
            for (auto& w : ring)
            {
                if (w.source == source && w.message == message)
                {
                    ++w.count;
                    w.lastSeq = seq;
                    return false;
                }
            }

            VFXWarning w;
            w.source = std::string(source);
            w.message = std::string(message);
            w.count = 1;
            w.lastSeq = seq;
            if (ring.size() >= capacity)
                ring.erase(ring.begin()); // drop the oldest distinct warning
            ring.push_back(std::move(w));
            return true;
        }

        // Most-recent distinct warnings, oldest-first (insertion order). The UI may
        // sort by lastSeq for recency.
        std::vector<VFXWarning> recent(size_t maxCount = 64) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (ring.size() <= maxCount)
                return ring;
            return std::vector<VFXWarning>(ring.end() - static_cast<std::ptrdiff_t>(maxCount), ring.end());
        }

        size_t distinctCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return ring.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex);
            ring.clear();
        }

    private:
        VFXRuntimeDiagnostics() = default;

        static constexpr size_t capacity = 64;
        mutable std::mutex mutex;
        std::vector<VFXWarning> ring;
        uint64_t seqCounter = 0;
    };
}
