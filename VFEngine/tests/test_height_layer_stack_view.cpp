// VK-1648 — the reserved height-layer list's arithmetic.
//
// These three functions were pulled out of HeightLayerPanel because the Tests project links
// Services but NOT the Editor (premake5.lua), so anything left inside an ImGui draw() is
// unreachable — and two of the story's acceptance criteria ("selection after deletion" and
// "progress-state transitions") are about exactly this logic. Pure functions, no dispatcher, no
// grid, no Vulkan.

#include <doctest.h>

#include <data/HeightLayerStackView.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    services::HeightLayerInfo layer(uint64_t id, uint32_t order, bool visible = true)
    {
        services::HeightLayerInfo info;
        info.id = id;
        info.order = order;
        info.visible = visible;
        info.affectedTileCount = 4;
        info.name = "Layer " + std::to_string(id);
        return info;
    }

    // Four rows, ids deliberately non-contiguous and unordered relative to position: an
    // implementation that confused an id with an index would pass on 1,2,3,4.
    std::vector<services::HeightLayerInfo> fourLayers()
    {
        return {layer(11, 0), layer(7, 1), layer(42, 2), layer(3, 3)};
    }
}

TEST_SUITE("HeightLayerStackView")
{
    TEST_CASE("height_layer_selection: deleting the selected row selects the one below it")
    {
        const auto stack = fourLayers();

        SUBCASE("a middle row hands selection downwards")
        {
            CHECK(services::selectionAfterDelete(stack, 7, 7) == 42);
        }

        SUBCASE("the first row hands selection downwards too")
        {
            CHECK(services::selectionAfterDelete(stack, 11, 11) == 7);
        }

        SUBCASE("the last row falls back to the one above")
        {
            CHECK(services::selectionAfterDelete(stack, 3, 3) == 42);
        }

        SUBCASE("deleting the only row leaves nothing selected")
        {
            const std::vector<services::HeightLayerInfo> single{layer(9, 0)};
            CHECK(services::selectionAfterDelete(single, 9, 9) == 0);
        }

        SUBCASE("deleting a row that is not selected does not move the selection")
        {
            CHECK(services::selectionAfterDelete(stack, 42, 7) == 7);
        }

        SUBCASE("an id the stack does not carry changes nothing")
        {
            // Nothing was deleted, so nothing may move — including when the caller passes the id
            // it is about to try to delete.
            CHECK(services::selectionAfterDelete(stack, 999, 7) == 7);
            CHECK(services::selectionAfterDelete(stack, 999, 999) == 999);
        }

        SUBCASE("an empty stack is just the unknown-id case, not a special one")
        {
            // The selection is returned UNCHANGED rather than cleared. An empty stack means the
            // delete found nothing to delete, and this function's whole contract is "what moves
            // when `deletedId` leaves `stack`" — if it never left, nothing moves.
            //
            // Clearing here instead would be a second, contradictory rule for a case the panel
            // never reaches: it only calls this while actually deleting a row it is looking at,
            // and refresh() independently drops a selection whose layer is gone.
            CHECK(services::selectionAfterDelete({}, 11, 11) == 11);

            // Deleting the last REAL row is the case that clears the selection, and it is
            // covered above by "deleting the only row leaves nothing selected".
        }

        SUBCASE("repeatedly deleting the selection walks down and then stops")
        {
            // The behaviour an artist actually sees when clearing a stack from the top.
            auto working = fourLayers();
            uint64_t selection = 11;

            selection = services::selectionAfterDelete(working, 11, selection);
            CHECK(selection == 7);
            working.erase(working.begin());

            selection = services::selectionAfterDelete(working, 7, selection);
            CHECK(selection == 42);
            working.erase(working.begin());

            selection = services::selectionAfterDelete(working, 42, selection);
            CHECK(selection == 3);
            working.erase(working.begin());

            selection = services::selectionAfterDelete(working, 3, selection);
            CHECK(selection == 0);
        }
    }

    TEST_CASE("height_layer_reorder: an insert zone is not the destination index")
    {
        const auto stack = fourLayers(); // [11, 7, 42, 3]

        SUBCASE("dragging DOWN shifts by one — this is the off-by-one")
        {
            // Zone 3 means "land above the row currently at index 3", i.e. between 42 and 3. Once
            // 11 is lifted out the list is [7, 42, 3], and landing above 3 is index 2, not 3.
            const auto target = services::moveTargetForInsertZone(stack, 11, 3);
            REQUIRE(target.has_value());
            CHECK(*target == 2);
        }

        SUBCASE("dragging to the very bottom")
        {
            const auto target = services::moveTargetForInsertZone(stack, 11, 4);
            REQUIRE(target.has_value());
            CHECK(*target == 3);
        }

        SUBCASE("dragging UP is the identity — nothing below has moved")
        {
            const auto target = services::moveTargetForInsertZone(stack, 3, 1);
            REQUIRE(target.has_value());
            CHECK(*target == 1);
        }

        SUBCASE("dragging to the very top")
        {
            const auto target = services::moveTargetForInsertZone(stack, 3, 0);
            REQUIRE(target.has_value());
            CHECK(*target == 0);
        }

        SUBCASE("both zones flanking the dragged row are no-ops")
        {
            // moveLayer would accept either and report success, so filtering here is what stops a
            // stray drop recording an undo entry that changes nothing.
            CHECK_FALSE(services::moveTargetForInsertZone(stack, 7, 1).has_value());
            CHECK_FALSE(services::moveTargetForInsertZone(stack, 7, 2).has_value());
        }

        SUBCASE("an unknown id and an out-of-range zone are both refused")
        {
            CHECK_FALSE(services::moveTargetForInsertZone(stack, 999, 2).has_value());
            CHECK_FALSE(services::moveTargetForInsertZone(stack, 11, 5).has_value());
            CHECK_FALSE(services::moveTargetForInsertZone({}, 11, 0).has_value());
        }

        SUBCASE("every legal drop round-trips through an actual list splice")
        {
            // The property the arithmetic exists for: applying the returned index the way
            // TerrainHeightLayerStore::moveLayer does must land the row where the zone pointed.
            for (uint64_t draggedId : {11ull, 7ull, 42ull, 3ull})
            {
                for (uint32_t zone = 0; zone <= 4; ++zone)
                {
                    const auto target = services::moveTargetForInsertZone(stack, draggedId, zone);
                    if (!target)
                        continue;

                    // What the artist aimed at: the row that was at `zone`, or the end.
                    const bool droppedAtEnd = (zone == stack.size());
                    const uint64_t landsAbove = droppedAtEnd ? 0 : stack[zone].id;

                    std::vector<uint64_t> ids;
                    for (const auto& info : stack)
                        if (info.id != draggedId)
                            ids.push_back(info.id);
                    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(*target), draggedId);

                    const auto placed = std::find(ids.begin(), ids.end(), draggedId);
                    REQUIRE(placed != ids.end());
                    if (droppedAtEnd)
                    {
                        CHECK(placed + 1 == ids.end());
                    }
                    else
                    {
                        REQUIRE(placed + 1 != ids.end());
                        CHECK(*(placed + 1) == landsAbove);
                    }
                }
            }
        }
    }

    TEST_CASE("height_layer_progress: the fraction is latched and monotone")
    {
        services::RecomposeProgressLatch latch;

        SUBCASE("idle reports complete and inactive")
        {
            const auto idle = services::advanceRecomposeProgress(latch, 0, 0, 0);
            CHECK_FALSE(idle.active);
            CHECK(idle.progress == doctest::Approx(1.0f));
            CHECK(idle.totalAtStart == 0);
            CHECK(latch.peak == 0);
        }

        SUBCASE("the first poll latches the peak and reports zero progress")
        {
            const auto first = services::advanceRecomposeProgress(latch, 0, 0, 40);
            CHECK(first.active);
            CHECK(first.totalAtStart == 40);
            CHECK(first.progress == doctest::Approx(0.0f));
            CHECK(first.meshBacklog == 40);
        }

        SUBCASE("the fraction advances as the backlog drains, then resets")
        {
            const auto start = services::advanceRecomposeProgress(latch, 0, 0, 40);
            CHECK(start.totalAtStart == 40);

            const auto half = services::advanceRecomposeProgress(latch, 0, 0, 20);
            CHECK(half.active);
            CHECK(half.totalAtStart == 40);
            CHECK(half.progress == doctest::Approx(0.5f));

            const auto done = services::advanceRecomposeProgress(latch, 0, 0, 0);
            CHECK_FALSE(done.active);
            CHECK(done.progress == doctest::Approx(1.0f));

            // Reset, so the next operation gets its own scale rather than inheriting the largest
            // one this session has ever seen.
            CHECK(latch.peak == 0);
            const auto next = services::advanceRecomposeProgress(latch, 0, 0, 8);
            CHECK(next.totalAtStart == 8);
        }

        SUBCASE("work arriving mid-drain raises the peak and never rewinds the bar")
        {
            const auto start = services::advanceRecomposeProgress(latch, 0, 0, 40);
            CHECK(start.totalAtStart == 40);
            const auto half = services::advanceRecomposeProgress(latch, 0, 0, 20);

            // A second invalidation lands: 60 outstanding against a peak that was 40.
            const auto surge = services::advanceRecomposeProgress(latch, 0, 0, 60);
            CHECK(surge.totalAtStart == 60);
            CHECK(surge.progress == doctest::Approx(0.0f));
            CHECK(surge.progress <= half.progress); // it may stall, but it must not go backwards

            const auto later = services::advanceRecomposeProgress(latch, 0, 0, 15);
            CHECK(later.totalAtStart == 60);
            CHECK(later.progress == doctest::Approx(0.75f));
        }

        SUBCASE("both drainable counts feed the fraction")
        {
            const auto mixed = services::advanceRecomposeProgress(latch, 10, 0, 30);
            CHECK(mixed.totalAtStart == 40);
            CHECK(mixed.pendingResident == 10);
            CHECK(mixed.meshBacklog == 30);
        }

        SUBCASE("an unloaded-only backlog is reported but neither activates nor pins the bar")
        {
            // These wait on the streamer, not on the recompose budget. Folded into the fraction
            // they would hold the bar below 100% for as long as the camera stayed away, which
            // reads as a hang rather than as "not loaded".
            const auto unloaded = services::advanceRecomposeProgress(latch, 0, 25, 0);
            CHECK_FALSE(unloaded.active);
            CHECK(unloaded.progress == doctest::Approx(1.0f));
            CHECK(unloaded.pendingUnloaded == 25);
            CHECK(unloaded.totalAtStart == 0);
            CHECK(latch.peak == 0);
        }
    }
}
