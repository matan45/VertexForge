#include "HeightLayerStackView.hpp"

#include <algorithm>

namespace services
{
    namespace
    {
        // Position of `id` in the stack, or nullopt. The stack is a handful of rows in practice
        // (a road each), so a linear scan is the right shape and matches what the store itself does.
        std::optional<size_t> indexOf(const std::vector<HeightLayerInfo>& stack, uint64_t id)
        {
            const auto it = std::find_if(stack.begin(), stack.end(),
                                         [id](const HeightLayerInfo& info) { return info.id == id; });
            if (it == stack.end())
                return std::nullopt;

            return static_cast<size_t>(std::distance(stack.begin(), it));
        }
    }

    uint64_t selectionAfterDelete(const std::vector<HeightLayerInfo>& stack, uint64_t deletedId,
                                  uint64_t currentSelection)
    {
        const std::optional<size_t> index = indexOf(stack, deletedId);
        if (!index)
            return currentSelection; // nothing was deleted, so nothing moves

        if (currentSelection != deletedId)
            return currentSelection; // some other row went; the selected one is still there

        // Prefer the row BELOW. Repeatedly deleting then walks down the list, which is what every
        // other list in the editor does and what an artist clearing a stack expects.
        if (*index + 1 < stack.size())
            return stack[*index + 1].id;

        // It was the last row: fall back to the one above it.
        if (*index > 0)
            return stack[*index - 1].id;

        return 0; // the stack is emptying; 0 is the "nothing selected" sentinel
    }

    std::optional<uint32_t> moveTargetForInsertZone(const std::vector<HeightLayerInfo>& stack,
                                                    uint64_t draggedId, uint32_t zone)
    {
        const std::optional<size_t> from = indexOf(stack, draggedId);
        if (!from)
            return std::nullopt;

        if (static_cast<size_t>(zone) > stack.size())
            return std::nullopt;

        // The two zones flanking the dragged row both mean "leave it exactly where it is".
        // moveLayer would happily accept the resulting index and report success, so filtering here
        // is what stops a stray click recording an undo entry for a move that moved nothing.
        if (static_cast<size_t>(zone) == *from || static_cast<size_t>(zone) == *from + 1)
            return std::nullopt;

        // Dragging DOWNWARDS: the row is lifted out before it is re-inserted, so every zone below
        // its old position has already shifted up by one by the time moveLayer counts.
        const size_t target = (static_cast<size_t>(zone) > *from) ? static_cast<size_t>(zone) - 1
                                                                  : static_cast<size_t>(zone);
        return static_cast<uint32_t>(target);
    }

    HeightLayerRecomposeProgress advanceRecomposeProgress(RecomposeProgressLatch& latch,
                                                          uint32_t pendingResident,
                                                          uint32_t pendingUnloaded,
                                                          uint32_t meshBacklog)
    {
        HeightLayerRecomposeProgress result;
        result.pendingResident = pendingResident;
        result.pendingUnloaded = pendingUnloaded;
        result.meshBacklog = meshBacklog;

        // Only the two that actually drain feed the fraction. pendingUnloaded is waiting on the
        // streamer, and a tile the camera never revisits would otherwise hold the bar short of
        // 100% forever.
        const uint32_t outstanding = pendingResident + meshBacklog;
        if (outstanding == 0)
        {
            latch.peak = 0;
            return result; // progress 1.0, active false
        }

        // Latched high-water mark, so the fraction is monotone even though a recompose marks fresh
        // mesh work as it goes and new invalidations can arrive mid-drain.
        latch.peak = std::max(latch.peak, outstanding);

        result.totalAtStart = latch.peak;
        result.progress =
            1.0f - static_cast<float>(outstanding) / static_cast<float>(latch.peak);
        result.active = true;
        return result;
    }
}
