#pragma once
#include "data/EntityHandle.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

// VK-1490: pure, ImGui-free ordered-selection policy shared by the Scene Graph
// panel and the editor Viewport. A selection is an ordered vector whose front()
// is the active/primary entity (what the Details panel, gizmo pivot and other
// single-target consumers operate on).
//
// Every function is pure — no ImGui, no EventDispatcher, no registry — so the
// policy is directly unit-testable (the Tests project includes VFEngine/editor).
namespace selection
{
    struct Modifiers
    {
        bool ctrl = false;
        bool shift = false;
    };

    inline bool contains(const std::vector<services::EntityHandle>& sel,
                         services::EntityHandle handle)
    {
        for (const auto& h : sel)
        {
            if (h.id == handle.id) return true;
        }
        return false;
    }

    // Click on a pickable entity.
    //  - plain: replace with {clicked}
    //  - ctrl:  toggle — if selected, remove it (order preserved, so the next
    //           entity promotes to active via front()); otherwise clicked
    //           becomes the new active at front, rest keep their order
    //  - shift: additive — clicked becomes active at front; if it was already
    //           selected it is promoted to front without duplication
    // Shift wins over ctrl when both are held (mirrors the Scene Graph panel,
    // where the shift-range branch is checked before the ctrl branch).
    inline std::vector<services::EntityHandle> computeClickSelection(
        const std::vector<services::EntityHandle>& current,
        services::EntityHandle clicked,
        Modifiers mods)
    {
        std::vector<services::EntityHandle> result;

        if (mods.shift)
        {
            result.reserve(current.size() + 1);
            result.push_back(clicked);
            for (const auto& h : current)
            {
                if (h.id != clicked.id) result.push_back(h);
            }
            return result;
        }

        if (mods.ctrl)
        {
            if (contains(current, clicked))
            {
                result.reserve(current.size());
                for (const auto& h : current)
                {
                    if (h.id != clicked.id) result.push_back(h);
                }
            }
            else
            {
                result.reserve(current.size() + 1);
                result.push_back(clicked);
                for (const auto& h : current) result.push_back(h);
            }
            return result;
        }

        result.push_back(clicked);
        return result;
    }

    // Click on empty space: plain clears, any modifier preserves the selection.
    inline std::vector<services::EntityHandle> computeEmptyClick(
        const std::vector<services::EntityHandle>& current,
        Modifiers mods)
    {
        if (mods.ctrl || mods.shift) return current;
        return {};
    }

    // Marquee release. Plain: the hits replace the selection (hit order kept,
    // duplicates collapsed, first hit becomes active). Ctrl/Shift: additive —
    // current order first, then the hits that are not already selected; the
    // active entity is unchanged.
    inline std::vector<services::EntityHandle> computeMarqueeSelection(
        const std::vector<services::EntityHandle>& current,
        const std::vector<services::EntityHandle>& hits,
        Modifiers mods)
    {
        std::vector<services::EntityHandle> result;
        std::unordered_set<uint64_t> seen;
        result.reserve(current.size() + hits.size());

        if (mods.ctrl || mods.shift)
        {
            for (const auto& h : current)
            {
                if (seen.insert(h.id).second) result.push_back(h);
            }
        }
        for (const auto& h : hits)
        {
            if (seen.insert(h.id).second) result.push_back(h);
        }
        return result;
    }

    // Scene Graph Shift-range over the previous frame's visible row order,
    // anchor..clicked inclusive, clicked entity first (it becomes active).
    // nullopt when the anchor is unset or either row is not visible any more —
    // the caller falls back to its non-range behavior.
    inline std::optional<std::vector<services::EntityHandle>> computeRangeSelection(
        const std::vector<services::EntityHandle>& visibleOrder,
        services::EntityHandle anchor,
        services::EntityHandle clicked)
    {
        if (!anchor.isValid()) return std::nullopt;

        int anchorIndex = -1;
        int clickedIndex = -1;
        for (int i = 0; i < static_cast<int>(visibleOrder.size()); ++i)
        {
            if (visibleOrder[i].id == anchor.id) anchorIndex = i;
            if (visibleOrder[i].id == clicked.id) clickedIndex = i;
        }
        if (anchorIndex < 0 || clickedIndex < 0) return std::nullopt;

        const int lo = std::min(anchorIndex, clickedIndex);
        const int hi = std::max(anchorIndex, clickedIndex);

        std::vector<services::EntityHandle> result;
        result.reserve(static_cast<size_t>(hi - lo) + 1);
        result.push_back(clicked);
        for (int i = lo; i <= hi; ++i)
        {
            if (visibleOrder[i].id != clicked.id) result.push_back(visibleOrder[i]);
        }
        return result;
    }

    // Selected entities minus the root and minus any entity whose ancestor is
    // also selected — a group transform must move parent+child selections once,
    // not twice. parentOf: callable EntityHandle -> std::optional<EntityHandle>.
    // The ancestor walk is depth-bounded so malformed (cyclic) parent data
    // cannot hang the editor; a tripped bound treats the entity as top-level.
    template <typename ParentOf>
    std::vector<services::EntityHandle> collectTopLevel(
        const std::vector<services::EntityHandle>& sel,
        ParentOf&& parentOf,
        services::EntityHandle root)
    {
        constexpr int maxDepth = 4096;

        auto isAncestorSelected = [&](services::EntityHandle handle)
        {
            services::EntityHandle current = handle;
            for (int depth = 0; depth < maxDepth; ++depth)
            {
                std::optional<services::EntityHandle> parent = parentOf(current);
                if (!parent.has_value()) return false;
                current = *parent;
                if (contains(sel, current)) return true;
            }
            return false;
        };

        std::vector<services::EntityHandle> result;
        result.reserve(sel.size());
        for (const auto& handle : sel)
        {
            if (!handle.isValid() || handle.id == root.id) continue;
            if (!isAncestorSelected(handle)) result.push_back(handle);
        }
        return result;
    }
}
