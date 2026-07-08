#pragma once

// VK-1481 Phase 2 (VFX draw-call merge) — consecutive-run batching for the GPU VFX pipelines.
//
// After the per-emitter draws are placed in submission order, adjacent emitters whose slots are
// CONSECUTIVE (and, where applicable, share a batch key such as the blend-pipeline variant) can be
// collapsed into a single vkCmdDrawIndexedIndirect(drawCount = runLen). The shader recovers the
// per-sub-draw emitter as `runBaseSlot + gl_DrawID`.
//
// This is the exact run-finding condition that previously lived, copy-pasted, in the scene, ribbon
// and distortion render paths. Consolidating it here keeps the merge rule in one place. Pure /
// CPU-testable — no Vulkan, no render types — so both the pipelines and the unit tests include it.

#include <cstdint>
#include <vector>

namespace vfx
{
    // Keyed merge: `slots` holds each drawable's emitter slot in submission order; `keys` is a
    // parallel, equal-length array of per-drawable batch keys (e.g. a pipeline-variant id). A run
    // extends while the next slot is `prev + 1` AND its key matches the run's key. `emit` is called
    // once per run as `emit(uint32_t baseSlot, uint32_t runLen, uint32_t key)`. Zero allocation.
    template <typename EmitFn>
    void forEachDrawRun(const std::vector<uint32_t>& slots,
                        const std::vector<uint32_t>& keys,
                        EmitFn&& emit)
    {
        const size_t n = slots.size();
        size_t i = 0;
        while (i < n)
        {
            const uint32_t baseSlot = slots[i];
            const uint32_t key = keys[i];
            size_t j = i + 1;
            while (j < n && slots[j] == slots[j - 1] + 1 && keys[j] == key)
            {
                ++j;
            }
            emit(baseSlot, static_cast<uint32_t>(j - i), key);
            i = j;
        }
    }

    // Key-less overload for single-pipeline passes (e.g. distortion): a run extends on slot
    // consecutiveness alone. `emit` is called as `emit(uint32_t baseSlot, uint32_t runLen)`.
    template <typename EmitFn>
    void forEachDrawRun(const std::vector<uint32_t>& slots, EmitFn&& emit)
    {
        const size_t n = slots.size();
        size_t i = 0;
        while (i < n)
        {
            const uint32_t baseSlot = slots[i];
            size_t j = i + 1;
            while (j < n && slots[j] == slots[j - 1] + 1)
            {
                ++j;
            }
            emit(baseSlot, static_cast<uint32_t>(j - i));
            i = j;
        }
    }
}
