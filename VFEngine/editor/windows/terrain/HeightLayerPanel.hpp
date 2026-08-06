#pragma once

#include "data/TerrainData.hpp"
#include "events/EventTypes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace windows
{
    // VK-1648. The reserved height-layer stack, as an editable list: visibility, ordering,
    // selection, rename and delete.
    //
    // Standalone rather than a section inside SplineToolPanel, because the stack OUTLIVES splines.
    // After a reload it comes back off the .vfterrainlayers sidecar with no active spline and no
    // applied-spline bookkeeping behind any of its rows — that vector is session-only and nothing
    // repopulates it. Reaching a section in the spline panel would also mean entering spline
    // authoring mode, which arms click-to-add-point on the terrain, so hiding a layer would put the
    // artist one stray click away from an edit; and SplineToolPanel exits spline mode when its
    // window closes, which would take the list with it.
    //
    // Talks to Services only. It includes no VF_TERRAIN_API header — the Editor does not link
    // Terrain.dll — so everything it knows about a layer is services::HeightLayerInfo plus the
    // commands in SplineTerrainEvents.hpp. That is enforced by the linker, not by convention.
    class HeightLayerPanel
    {
    private:
        bool visible = false;

        // A CACHE of the service's stack, refreshed on HeightLayerStackChangedNotification — never
        // a second copy that can be edited. Same rule SplineToolPanel follows: one source of truth,
        // and every mutation is a command.
        std::vector<services::HeightLayerInfo> stack;
        bool stackDirty = true;

        uint64_t selectedId = 0;

        uint64_t renamingId = 0;
        bool renameFocusPending = false;
        // Sized to terrain::MAX_LAYER_NAME_LENGTH, the sidecar's cap. Anything longer would be
        // silently truncated on save, so the field refuses it up front instead.
        std::array<char, 128> renameBuffer{};

        // Deferred mutations. Every one of these reorders or shortens `stack` under the row
        // iterator, so they are captured during the loop and dispatched after it — the discipline
        // UIDropdownDrawer uses, rather than SplineToolPanel's break-out-of-the-loop.
        //
        // At most one is armed per frame: they all originate from a click, and ImGui delivers one
        // activation per widget per frame.
        uint64_t pendingDeleteId = 0;
        uint64_t pendingVisibilityId = 0;
        bool pendingVisibilityValue = false;
        uint64_t pendingMoveId = 0;
        uint32_t pendingMoveIndex = 0;
        uint64_t pendingRenameId = 0;
        std::string pendingRenameValue;

        events::SubscriptionToken stackToken;
        bool subscribed = false;

    public:
        HeightLayerPanel() = default;
        ~HeightLayerPanel();

        void draw();
        void show()
        {
            visible = true;
            stackDirty = true;
        }

    private:
        void subscribe();
        void refresh();

        void drawRow(const services::HeightLayerInfo& info, size_t index, bool interactive);
        void drawInsertZone(size_t zone);
        void applyPendingEdits();

        // Row label: the artist's name when there is one, otherwise the id. Never empty — a blank
        // row is unclickable and unidentifiable.
        [[nodiscard]] static std::string displayName(const services::HeightLayerInfo& info);
    };
}
