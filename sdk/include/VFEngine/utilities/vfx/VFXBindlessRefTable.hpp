#pragma once

// VK-1481 (VFX bindless) — refcount + deferred-teardown bookkeeping for the shared
// VFX bindless texture table.
//
// The engine's render::gpudriven::BindlessTextureManager dedups textures by key and
// hands out a stable slot index, but it does NOT refcount: a single unregister frees
// the slot even if other emitters still reference it. The four VFX GPU pipelines used
// to each carry their own per-path refcounted TextureEntry map; this pure header
// consolidates that refcount + 3-frame deferral logic into one CPU-testable state
// machine so the device wrapper (render::vfx::VFXBindlessTextures) only has to own the
// Vulkan resources.
//
// Slot INDEX ALLOCATION stays with the real manager: on a first acquire the wrapper
// registers the texture, gets a slot, and feeds it back via setIndex(). This header
// tracks who still references each key and when a released key is safe to tear down.
//
// Deferral mirrors the existing VFX idioms: teardown is delayed FRAMES_BEFORE_DELETE
// frames (release schedules @frame, collectReady(frame) reports it once the window has
// passed) using the same unsigned-wrap arithmetic as VFXSceneRenderer's pendingEmitterFrees.
//
// Pure / no Vulkan / no core:: — both the wrapper and the doctest unit test include it.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vfx
{
    class VFXBindlessRefTable
    {
    public:
        // Sentinel for "no slot yet" (a fresh key between acquire() and setIndex()).
        static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

        explicit VFXBindlessRefTable(uint32_t framesBeforeDelete)
            : framesBeforeDelete_(framesBeforeDelete)
        {
        }

        struct AcquireResult
        {
            bool needsRegister = false; // caller must load+register, then call setIndex()
            uint32_t index = kInvalidIndex; // valid only when needsRegister == false
        };

        // Take a reference on key. If the key is new, returns {needsRegister:true} and the
        // caller must register the GPU texture then call setIndex(); the entry is live at
        // refCount 1 in the meantime. If the key already exists (live OR pending teardown),
        // increments its refCount, cancels any scheduled teardown, and returns its slot.
        AcquireResult acquire(const std::string& key)
        {
            auto it = entries_.find(key);
            if (it != entries_.end())
            {
                Entry& e = it->second;
                ++e.refCount;
                e.pending = false; // re-acquire cancels a scheduled teardown; texture stays resident
                return {false, e.index};
            }
            entries_.emplace(key, Entry{kInvalidIndex, 1, false, 0});
            return {true, kInvalidIndex};
        }

        // Record the GPU slot for a freshly-acquired key (the one acquire() flagged needsRegister).
        void setIndex(const std::string& key, uint32_t idx)
        {
            auto it = entries_.find(key);
            if (it != entries_.end())
            {
                it->second.index = idx;
            }
        }

        // Roll back a fresh key whose GPU registration failed (missing file, table full, load
        // throw). Valid immediately after a needsRegister acquire, before setIndex().
        void fail(const std::string& key)
        {
            entries_.erase(key);
        }

        // Drop a reference on key. On the last reference (refCount hits 0) the key is marked
        // pending and scheduled for teardown at `frame`. A double-release, or a release of an
        // absent/already-pending key, is a no-op (never underflows).
        void release(const std::string& key, uint32_t frame)
        {
            auto it = entries_.find(key);
            if (it == entries_.end() || it->second.refCount == 0)
            {
                return;
            }
            Entry& e = it->second;
            if (--e.refCount == 0)
            {
                e.pending = true;
                e.retiredFrame = frame;
            }
        }

        // Keys whose teardown window has elapsed (pending && frame - retiredFrame >= N), each
        // with the slot to unregister. The caller unregisters the GPU slot then calls forget().
        [[nodiscard]] std::vector<std::pair<std::string, uint32_t>> collectReady(uint32_t frame) const
        {
            std::vector<std::pair<std::string, uint32_t>> ready;
            for (const auto& [key, e] : entries_)
            {
                if (e.pending && (frame - e.retiredFrame) >= framesBeforeDelete_)
                {
                    ready.emplace_back(key, e.index);
                }
            }
            return ready;
        }

        // Erase a key after its GPU slot has been unregistered.
        void forget(const std::string& key)
        {
            entries_.erase(key);
        }

        // --- introspection (primarily for unit tests) ---

        [[nodiscard]] std::optional<uint32_t> indexOf(const std::string& key) const
        {
            auto it = entries_.find(key);
            if (it == entries_.end() || it->second.index == kInvalidIndex)
            {
                return std::nullopt;
            }
            return it->second.index;
        }

        [[nodiscard]] uint32_t refCountOf(const std::string& key) const
        {
            auto it = entries_.find(key);
            return it == entries_.end() ? 0u : it->second.refCount;
        }

        [[nodiscard]] bool isPending(const std::string& key) const
        {
            auto it = entries_.find(key);
            return it != entries_.end() && it->second.pending;
        }

        [[nodiscard]] size_t liveCount() const { return entries_.size(); }

    private:
        struct Entry
        {
            uint32_t index = kInvalidIndex;
            uint32_t refCount = 0;
            bool pending = false;      // refCount hit 0, teardown scheduled
            uint32_t retiredFrame = 0; // frame at which the last reference was dropped
        };

        std::unordered_map<std::string, Entry> entries_;
        uint32_t framesBeforeDelete_;
    };
}
