#pragma once

#include "StreamingPriority.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

namespace streaming
{
    // VK-1600: capacity-bounded residency pool with priority-ranked, LRU-tiebroken eviction —
    // the "what may stay resident" decision the streaming systems share (prefetched sector
    // blobs, HLOD proxy geometry, activated sectors).
    //
    // DECISION ONLY. admit() and trim() return the keys the caller must release; the pool
    // never touches the payload. Same contract SectorStreamer has with its actions, and the
    // reason this can sit in Utilities while its consumers live in Services.
    //
    // `cost` is deliberately unitless: bytes for a memory budget, 1 per entry for a count
    // budget. A capacity of 0 means UNLIMITED - the pool is inert and admit() always
    // succeeds - which is the "0 = today's behaviour" sentinel every other streaming budget
    // uses.
    //
    // `priority` is HIGHER = keep, matching PriorityHysteresis, which this reuses rather than
    // inventing a second margin convention. Admission is STRICT IMPROVEMENT: a candidate may
    // only displace entries it beats by MORE than the margin. That is what makes the
    // evict/re-request livelock structurally impossible rather than merely damped - within a
    // frame the ranking is a strict total order, so A cannot evict B and then B evict A.
    //
    // LRU governs the ORDER legal victims are picked in, not whether eviction happens: pass a
    // constant priority and admit() refuses at capacity rather than rotating the contents,
    // which is the correct degenerate behaviour for a ring (rotating would be exactly the
    // livelock). trim() is the pure-LRU path - it has no candidate to beat, so it evicts
    // worst-then-oldest until the pool fits.
    //
    // Single-threaded by design, like its siblings: drive it from the owning update loop.
    template <typename Key, typename Hash = std::hash<Key>>
    class BudgetedEvictionPool
    {
    public:
        struct Entry
        {
            uint64_t cost = 0;
            uint64_t lastUsedFrame = 0;
            float priority = 0.0f; // higher = keep
            // Never a victim: an in-flight read, a dirty sector, anything the editor holds.
            bool pinned = false;
            // Insertion order, purely so the eviction comparator is a TOTAL order. Without it
            // entries that tie on (priority, lastUsedFrame) would be ordered by unordered_map
            // iteration and the same scene would evict differently run to run.
            uint64_t insertSeq = 0;
        };

        enum class Admission
        {
            Admitted,        // resident now; outEvicted names what had to go
            AlreadyResident, // was already in the pool; re-ranked, nothing evicted
            Refused          // did not fit without evicting something worth keeping
        };

        // 0 = unlimited. Deliberately does NOT evict - call trim() to bring an over-budget
        // pool back in line, so a shrink is still the caller's decision to execute.
        void setCapacity(uint64_t value) { capacityCost = value; }
        void setHysteresisMargin(float margin) { hysteresis.margin = margin; }

        // Admit `key`, evicting only entries strictly worse than `priority`. If those legal
        // victims cannot free enough, NOTHING is evicted and Refused is returned: a refusal
        // must never cost the caller residency it then fails to replace.
        Admission admit(const Key& key, uint64_t cost, uint64_t frame, float rawPriority,
                        std::vector<Key>& outEvicted)
        {
            const float priority = sanitizePriority(rawPriority);

            if (auto it = entries.find(key); it != entries.end())
            {
                usedCost = usedCost - it->second.cost + cost;
                it->second.cost = cost;
                it->second.lastUsedFrame = frame;
                it->second.priority = priority;
                return Admission::AlreadyResident;
            }

            if (capacityCost == 0)
            {
                insert(key, cost, frame, priority);
                return Admission::Admitted;
            }

            // Bigger than the whole pool: no eviction could ever make room, and evicting
            // everything first would be pure loss.
            if (cost > capacityCost)
                return Admission::Refused;

            if (usedCost + cost <= capacityCost)
            {
                insert(key, cost, frame, priority);
                return Admission::Admitted;
            }

            const uint64_t deficit = usedCost + cost - capacityCost;

            collectVictims(&priority);
            uint64_t freed = 0;
            size_t used = 0;
            for (; used < scratch.size() && freed < deficit; ++used)
                freed += scratch[used].cost;

            if (freed < deficit)
                return Admission::Refused;

            evictFront(used, outEvicted);
            insert(key, cost, frame, priority);
            return Admission::Admitted;
        }

        // Record an entry that is ALREADY resident, with no capacity check and no eviction.
        // Distinct from admit(), which asks permission: this states a fact. Used where
        // something became resident by a route the budget does not govern - an explicit,
        // user-requested load - so the pool's count stays honest and that entry becomes a
        // legal victim later rather than being invisible. May leave the pool over budget.
        void put(const Key& key, uint64_t cost, uint64_t frame, float rawPriority, bool pinned)
        {
            const float priority = sanitizePriority(rawPriority);
            auto it = entries.find(key);
            if (it == entries.end())
            {
                insert(key, cost, frame, priority);
                entries.find(key)->second.pinned = pinned;
                return;
            }
            usedCost = usedCost - it->second.cost + cost;
            it->second.cost = cost;
            it->second.lastUsedFrame = frame;
            it->second.priority = priority;
            it->second.pinned = pinned;
        }

        // Re-rank a resident entry against this frame's sources. The pool has no idea where
        // anything is, so the owner has to push distance in every frame; without it a pool
        // whose camera moved would evict on last frame's ranking.
        bool touch(const Key& key, uint64_t frame, float rawPriority)
        {
            auto it = entries.find(key);
            if (it == entries.end())
                return false;
            it->second.lastUsedFrame = frame;
            it->second.priority = sanitizePriority(rawPriority);
            return true;
        }

        bool setPinned(const Key& key, bool pinned)
        {
            auto it = entries.find(key);
            if (it == entries.end())
                return false;
            it->second.pinned = pinned;
            return true;
        }

        // Correct an entry's cost in place - an estimate replaced by the real size once the
        // bytes land. May legitimately push the pool over capacity; the next trim() settles it.
        bool resize(const Key& key, uint64_t cost)
        {
            auto it = entries.find(key);
            if (it == entries.end())
                return false;
            usedCost = usedCost - it->second.cost + cost;
            it->second.cost = cost;
            return true;
        }

        bool remove(const Key& key)
        {
            auto it = entries.find(key);
            if (it == entries.end())
                return false;
            usedCost -= it->second.cost;
            entries.erase(it);
            return true;
        }

        // Resets the eviction counter too: it is read as "how much churn has this WORLD
        // caused", and every caller of clear() is a world load / unload / mode change.
        void clear()
        {
            entries.clear();
            usedCost = 0;
            evictions = 0;
        }

        // Evict worst-first until the pool fits. There is no candidate here, so the
        // strict-improvement rule does not apply - this is the capacity-shrink path, and
        // something has to go. Pinned entries are still untouchable, so the pool can
        // legitimately come out of this still over budget.
        void trim(std::vector<Key>& outEvicted)
        {
            if (capacityCost == 0 || usedCost <= capacityCost)
                return;

            const uint64_t deficit = usedCost - capacityCost;

            collectVictims(nullptr);
            uint64_t freed = 0;
            size_t used = 0;
            for (; used < scratch.size() && freed < deficit; ++used)
                freed += scratch[used].cost;

            evictFront(used, outEvicted);
        }

        [[nodiscard]] bool contains(const Key& key) const { return entries.contains(key); }

        [[nodiscard]] const Entry* find(const Key& key) const
        {
            auto it = entries.find(key);
            return it == entries.end() ? nullptr : &it->second;
        }

        [[nodiscard]] size_t size() const { return entries.size(); }
        [[nodiscard]] bool empty() const { return entries.empty(); }
        [[nodiscard]] uint64_t residentCost() const { return usedCost; }
        [[nodiscard]] uint64_t capacity() const { return capacityCost; }
        [[nodiscard]] bool unlimited() const { return capacityCost == 0; }
        [[nodiscard]] bool overBudget() const { return capacityCost != 0 && usedCost > capacityCost; }
        // Evictions since the last clear(), for the streaming overlay: a counter that keeps
        // ticking while the camera stands still is the signature of a budget too small for
        // the ring.
        [[nodiscard]] uint64_t evictionCount() const { return evictions; }

        // Visit every resident entry. The owning system uses this to re-rank and re-pin the
        // whole pool once per frame.
        template <typename Fn>
        void forEach(Fn&& fn) const
        {
            for (const auto& [key, entry] : entries)
                fn(key, entry);
        }

    private:
        struct Victim
        {
            Key key;
            uint64_t cost = 0;
            uint64_t lastUsedFrame = 0;
            float priority = 0.0f;
            uint64_t insertSeq = 0;
        };

        // A NaN priority would make the eviction comparator violate strict weak ordering and
        // put std::sort into undefined behaviour. A NaN camera position is a real hazard here
        // (VK-1595 clamps the overlay grid for the same reason), so fold it to "worst
        // possible" - a NaN-ranked entry is exactly the one you want gone first.
        [[nodiscard]] static float sanitizePriority(float priority)
        {
            return priority == priority ? priority : -std::numeric_limits<float>::max();
        }

        void insert(const Key& key, uint64_t cost, uint64_t frame, float priority)
        {
            Entry entry;
            entry.cost = cost;
            entry.lastUsedFrame = frame;
            entry.priority = priority;
            entry.insertSeq = nextSeq++;
            entries.emplace(key, entry);
            usedCost += cost;
        }

        // Fill `scratch` with the evictable entries, worst first. `candidatePriority` non-null
        // applies the strict-improvement filter; null means "any unpinned entry will do",
        // which is the shrink path.
        void collectVictims(const float* candidatePriority)
        {
            scratch.clear();
            for (const auto& [key, entry] : entries)
            {
                if (entry.pinned)
                    continue;
                // shouldKeepActive is "within margin of the cutoff", so an entry the candidate
                // does not clearly beat keeps its slot - that inequality IS the hysteresis.
                if (candidatePriority &&
                    hysteresis.shouldKeepActive(entry.priority, *candidatePriority))
                    continue;
                scratch.push_back(Victim{key, entry.cost, entry.lastUsedFrame, entry.priority,
                                         entry.insertSeq});
            }

            std::sort(scratch.begin(), scratch.end(),
                      [](const Victim& a, const Victim& b)
                      {
                          if (a.priority != b.priority)
                              return a.priority < b.priority;       // least valuable first
                          if (a.lastUsedFrame != b.lastUsedFrame)
                              return a.lastUsedFrame < b.lastUsedFrame; // then least recently used
                          return a.insertSeq < b.insertSeq;         // then oldest, for determinism
                      });
        }

        void evictFront(size_t count, std::vector<Key>& outEvicted)
        {
            for (size_t i = 0; i < count; ++i)
            {
                auto it = entries.find(scratch[i].key);
                if (it == entries.end())
                    continue; // cannot happen; scratch is built from `entries` and nothing ran between
                usedCost -= it->second.cost;
                entries.erase(it);
                outEvicted.push_back(scratch[i].key);
                ++evictions;
            }
        }

        std::unordered_map<Key, Entry, Hash> entries;
        // Reused across calls: admit() runs every frame and a fresh vector per call would
        // allocate in the streaming hot path.
        std::vector<Victim> scratch;
        PriorityHysteresis hysteresis;
        uint64_t capacityCost = 0;
        uint64_t usedCost = 0;
        uint64_t evictions = 0;
        uint64_t nextSeq = 0;
    };

} // namespace streaming
