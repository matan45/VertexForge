#pragma once

#include "TerrainData.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace services
{
    // VK-1648. The arithmetic behind the reserved height-layer list, extracted out of the panel.
    //
    // Not a style choice: the Tests project links Services but NOT the Editor (premake5.lua), so
    // anything left inside an ImGui draw() is unreachable by the coverage this story owes —
    // "selection after deletion" and "progress-state transitions" are both acceptance criteria.
    // Pulling the three non-obvious computations out here makes them provable, and leaves the panel
    // as layout plus dispatch, which is the part a test could not check anyway.
    //
    // Everything here is pure. The caller owns all state.

    // Which layer the list should select once `deletedId` is gone from `stack`.
    //
    // `stack` is the list as it looks BEFORE the delete — the panel captures the pending id during
    // the row loop and applies it afterwards, so the post-delete list does not exist yet.
    //
    // The row below the deleted one, or the row above when it was the last, or 0 (nothing selected)
    // when the stack empties. Deleting a row that is not the selected one leaves the selection
    // alone, and so does an id the stack does not carry.
    [[nodiscard]] uint64_t selectionAfterDelete(const std::vector<HeightLayerInfo>& stack,
                                                uint64_t deletedId, uint64_t currentSelection);

    // Translates a drag-and-drop insert zone into MoveHeightLayerCommand::newIndex.
    //
    // Zones sit BETWEEN rows: zone i means "land above the row currently at index i", and
    // zone stack.size() means "land at the bottom". That is NOT the same number as the destination
    // index, because moveLayer takes the position in the list AFTER the dragged row is lifted out —
    // so every zone below the row it came from shifts up by one. Getting this wrong drops a row one
    // place short of where the artist aimed, but only when dragging downwards, which is exactly the
    // sort of bug that survives manual testing.
    //
    // nullopt when the drop changes nothing: the two zones flanking the dragged row are both
    // no-ops, and so is an id the stack does not carry or a zone past the end.
    [[nodiscard]] std::optional<uint32_t> moveTargetForInsertZone(
        const std::vector<HeightLayerInfo>& stack, uint64_t draggedId, uint32_t zone);

    // High-water mark behind HeightLayerRecomposeProgress::totalAtStart. Owned by the service and
    // passed in, so the fraction stays a pure function of (latch, counts).
    struct RecomposeProgressLatch
    {
        uint32_t peak = 0;
        // VK-1648. Latching the DENOMINATOR is not enough to keep the fraction monotone: when new
        // work lands mid-drain, `outstanding` and `peak` rise together and 1 - outstanding/peak
        // snaps back toward 0 — the bar visibly rewinds. So the reported fraction is latched too,
        // and reset with the peak when the backlog reaches zero.
        float reportedProgress = 0.0f;
    };

    // Turns the grid's three raw counts into the progress DTO, latching the peak so the fraction
    // is monotone even though a recompose queues fresh mesh work as it goes and new invalidations
    // can land mid-drain.
    //
    // `pendingUnloaded` is reported but deliberately kept OUT of the fraction: it waits on the
    // streamer, not on the recompose budget, so folding it in would pin the bar below 100% for as
    // long as the camera stays away — which reads as a hang rather than as "not loaded".
    //
    // Reaching zero resets the latch, so the next operation starts its own scale rather than
    // inheriting the largest one the session has ever seen.
    [[nodiscard]] HeightLayerRecomposeProgress advanceRecomposeProgress(
        RecomposeProgressLatch& latch, uint32_t pendingResident, uint32_t pendingUnloaded,
        uint32_t meshBacklog);
}
