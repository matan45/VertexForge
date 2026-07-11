#include <doctest.h>
#include <selection/SelectionPolicy.hpp>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <vector>

// ============================================================
// VK-1490: ordered multi-selection policy (editor/selection/SelectionPolicy.hpp).
//
// Pure CPU — no ImGui, no dispatcher, no registry. The selection is an ordered
// vector of EntityHandle whose front() is the active/primary entity; these
// cases pin the Unity-style modifier semantics shared by the Scene Graph panel
// and the Viewport.
// ============================================================

namespace
{
    services::EntityHandle handleOf(uint64_t id) { return services::EntityHandle{id}; }

    std::vector<services::EntityHandle> handlesOf(std::initializer_list<uint64_t> ids)
    {
        std::vector<services::EntityHandle> result;
        for (uint64_t id : ids) result.push_back(handleOf(id));
        return result;
    }

    std::vector<uint64_t> idsOf(const std::vector<services::EntityHandle>& handles)
    {
        std::vector<uint64_t> result;
        for (const auto& h : handles) result.push_back(h.id);
        return result;
    }
}

TEST_SUITE("SelectionPolicy") {

TEST_CASE("plain click replaces a multi-selection with the clicked entity") {
    const auto current = handlesOf({1, 2, 3});
    const auto result = selection::computeClickSelection(current, handleOf(5), {});
    CHECK(idsOf(result) == std::vector<uint64_t>{5});
}

TEST_CASE("ctrl-click on an unselected entity prepends it as the new active") {
    const auto current = handlesOf({1, 2, 3});
    const auto result =
        selection::computeClickSelection(current, handleOf(5), {.ctrl = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{5, 1, 2, 3});
}

TEST_CASE("ctrl-click on a selected non-active entity removes it, order kept") {
    const auto current = handlesOf({1, 2, 3});
    const auto result =
        selection::computeClickSelection(current, handleOf(2), {.ctrl = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{1, 3});
}

TEST_CASE("ctrl-click on the active entity promotes the next ordered entity") {
    const auto current = handlesOf({1, 2, 3});
    const auto result =
        selection::computeClickSelection(current, handleOf(1), {.ctrl = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{2, 3});
    // front() == 2 is the promoted active entity
}

TEST_CASE("ctrl-click removing the last selected entity leaves the selection empty") {
    const auto current = handlesOf({7});
    const auto result =
        selection::computeClickSelection(current, handleOf(7), {.ctrl = true});
    CHECK(result.empty());
}

TEST_CASE("shift-click adds an unselected entity as the new active") {
    const auto current = handlesOf({1, 2});
    const auto result =
        selection::computeClickSelection(current, handleOf(9), {.shift = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{9, 1, 2});
}

TEST_CASE("shift-click on an already selected entity promotes it without duplication") {
    const auto current = handlesOf({1, 2, 3});
    const auto result =
        selection::computeClickSelection(current, handleOf(3), {.shift = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{3, 1, 2});
}

TEST_CASE("shift wins over ctrl when both modifiers are held") {
    const auto current = handlesOf({1, 2});
    // ctrl alone would toggle 2 out; shift+ctrl must promote it instead
    const auto result = selection::computeClickSelection(
        current, handleOf(2), {.ctrl = true, .shift = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{2, 1});
}

TEST_CASE("empty click clears without modifiers and preserves with them") {
    const auto current = handlesOf({1, 2, 3});
    CHECK(selection::computeEmptyClick(current, {}).empty());
    CHECK(idsOf(selection::computeEmptyClick(current, {.ctrl = true})) ==
          std::vector<uint64_t>{1, 2, 3});
    CHECK(idsOf(selection::computeEmptyClick(current, {.shift = true})) ==
          std::vector<uint64_t>{1, 2, 3});
}

TEST_CASE("plain marquee replaces the selection with the hits in hit order") {
    const auto current = handlesOf({1, 2});
    const auto hits = handlesOf({4, 5, 6});
    const auto result = selection::computeMarqueeSelection(current, hits, {});
    CHECK(idsOf(result) == std::vector<uint64_t>{4, 5, 6});
}

TEST_CASE("plain marquee collapses duplicate hits, first occurrence wins") {
    const auto hits = handlesOf({4, 5, 4, 6, 5});
    const auto result = selection::computeMarqueeSelection({}, hits, {});
    CHECK(idsOf(result) == std::vector<uint64_t>{4, 5, 6});
}

TEST_CASE("ctrl marquee appends new hits, keeps order and the active entity") {
    const auto current = handlesOf({1, 2});
    const auto hits = handlesOf({2, 4, 1, 5});
    const auto result =
        selection::computeMarqueeSelection(current, hits, {.ctrl = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{1, 2, 4, 5});
    // active stays 1
}

TEST_CASE("ctrl marquee with only duplicate hits leaves the selection unchanged") {
    const auto current = handlesOf({1, 2, 3});
    const auto hits = handlesOf({3, 1});
    const auto result =
        selection::computeMarqueeSelection(current, hits, {.ctrl = true});
    CHECK(idsOf(result) == std::vector<uint64_t>{1, 2, 3});
}

TEST_CASE("range selection is anchor..clicked inclusive with clicked first") {
    const auto visible = handlesOf({10, 11, 12, 13, 14});
    const auto result =
        selection::computeRangeSelection(visible, handleOf(11), handleOf(13));
    REQUIRE(result.has_value());
    CHECK(idsOf(*result) == std::vector<uint64_t>{13, 11, 12});
}

TEST_CASE("range selection works with a reversed anchor/clicked order") {
    const auto visible = handlesOf({10, 11, 12, 13, 14});
    const auto result =
        selection::computeRangeSelection(visible, handleOf(13), handleOf(11));
    REQUIRE(result.has_value());
    CHECK(idsOf(*result) == std::vector<uint64_t>{11, 12, 13});
}

TEST_CASE("range selection with anchor == clicked selects just that row") {
    const auto visible = handlesOf({10, 11, 12});
    const auto result =
        selection::computeRangeSelection(visible, handleOf(11), handleOf(11));
    REQUIRE(result.has_value());
    CHECK(idsOf(*result) == std::vector<uint64_t>{11});
}

TEST_CASE("range selection falls back when the anchor is unset or not visible") {
    const auto visible = handlesOf({10, 11, 12});
    CHECK_FALSE(selection::computeRangeSelection(visible,
                                                 services::EntityHandle::invalid(),
                                                 handleOf(11))
                    .has_value());
    CHECK_FALSE(
        selection::computeRangeSelection(visible, handleOf(99), handleOf(11))
            .has_value());
    CHECK_FALSE(
        selection::computeRangeSelection(visible, handleOf(10), handleOf(99))
            .has_value());
}

// --- collectTopLevel -------------------------------------------------------
// Hierarchy fixture expressed as a child->parent map; the callback mirrors the
// GetEntityQuery-backed lambda production code passes in.

namespace
{
    auto parentOfMap(const std::map<uint64_t, uint64_t>& parents)
    {
        return [parents](services::EntityHandle h) -> std::optional<services::EntityHandle>
        {
            auto it = parents.find(h.id);
            if (it == parents.end()) return std::nullopt;
            return handleOf(it->second);
        };
    }
}

TEST_CASE("collectTopLevel drops a selected child of a selected parent") {
    // 0 = root; 1 under root; 2 under 1
    const std::map<uint64_t, uint64_t> parents{{1, 0}, {2, 1}};
    const auto sel = handlesOf({1, 2});
    const auto result = selection::collectTopLevel(sel, parentOfMap(parents), handleOf(0));
    CHECK(idsOf(result) == std::vector<uint64_t>{1});
}

TEST_CASE("collectTopLevel drops a grandchild when only the grandparent is selected") {
    // 1 under root, 2 under 1, 3 under 2; selected: {1, 3} — middle (2) unselected
    const std::map<uint64_t, uint64_t> parents{{1, 0}, {2, 1}, {3, 2}};
    const auto sel = handlesOf({1, 3});
    const auto result = selection::collectTopLevel(sel, parentOfMap(parents), handleOf(0));
    CHECK(idsOf(result) == std::vector<uint64_t>{1});
}

TEST_CASE("collectTopLevel keeps unrelated siblings and preserves order") {
    const std::map<uint64_t, uint64_t> parents{{1, 0}, {2, 0}, {3, 1}};
    const auto sel = handlesOf({2, 1, 3});
    const auto result = selection::collectTopLevel(sel, parentOfMap(parents), handleOf(0));
    CHECK(idsOf(result) == std::vector<uint64_t>{2, 1});
}

TEST_CASE("collectTopLevel excludes the root and invalid handles") {
    const std::map<uint64_t, uint64_t> parents{{1, 0}};
    auto sel = handlesOf({0, 1});
    sel.push_back(services::EntityHandle::invalid());
    const auto result = selection::collectTopLevel(sel, parentOfMap(parents), handleOf(0));
    // 1's ancestor 0 (root) IS selected here, so 1 is dropped too — matches the
    // Scene Graph panel semantics where a selected root covers its children.
    CHECK(result.empty());
}

TEST_CASE("collectTopLevel terminates on cyclic parent data") {
    // 1 hangs under a 2 <-> 3 cycle that never reaches a selected entity or a
    // parentless node; the depth bound must break the walk and keep 1.
    const std::map<uint64_t, uint64_t> parents{{1, 2}, {2, 3}, {3, 2}};
    const auto sel = handlesOf({1});
    const auto result = selection::collectTopLevel(sel, parentOfMap(parents), handleOf(0));
    CHECK(idsOf(result) == std::vector<uint64_t>{1});
}

} // TEST_SUITE
