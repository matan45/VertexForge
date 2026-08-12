#pragma once

#include <imgui.h>

namespace events::world
{
    struct StreamingOverlayCell;
    struct StreamingOverlaySnapshot;
}

namespace windows
{
    // VK-1595: a top-down schematic of the sector streaming ring, pinned in the viewport - the
    // engine's answer to UE5's wp.Runtime.ToggleDrawRuntimeHash2D. The 3D "Show Sector Bounds"
    // boxes remain a separate, complementary view.
    //
    // Drawn with the same recipe as ViewPortOverlay: a borderless, NoSavedSettings child window
    // opened inside ViewPort's own Begin("ViewPort"), painted through GetWindowDrawList().
    // Deliberately NOT GetForegroundDrawList() - that list is global and unclipped, so it paints
    // above every window regardless of overlap. The graph editors' zoom controls
    // (ShaderGraphEditorDraw / VFXGraphEditorDraw / AnimatorNodeGraph drawZoomControls) do use it
    // and will therefore paint over this panel when a floating graph editor overlaps the viewport;
    // that is pre-existing and affects all viewport content, not a reason to join them.
    //
    // Input-transparent (ImGuiWindowFlags_NoInputs) - see the flag comment in draw(). It is a
    // readout only; every control lives in the World Sectors window.
    //
    // Holds no per-frame heap state: the cell data arrives as borrowed pointers from
    // GetStreamingOverlaySnapshotQuery and every label goes through a stack buffer.
    class ViewPortStreamingOverlay
    {
    public:
        void draw();

    private:
        // Panel geometry. The grid is square and the cell size falls out of the ring width, so a
        // wide unload radius shrinks the cells rather than growing the panel.
        static constexpr float PANEL_SIZE = 208.0f;
        static constexpr float PANEL_MARGIN = 8.0f;

        static void drawGrid(const ::events::world::StreamingOverlaySnapshot& snapshot,
                             ImDrawList* drawList, const ImVec2& gridMin, float cellPx);
        static void drawRingsAndSources(const ::events::world::StreamingOverlaySnapshot& snapshot,
                                        ImDrawList* drawList, const ImVec2& gridMin, float cellPx);
        static void drawLegend();

        // State -> fill colour. Mirrors the Sector Grid tab's table (WorldSectorWindow.cpp) so the
        // two views never disagree about what a colour means.
        static ImU32 cellColor(const ::events::world::StreamingOverlayCell& cell);
    };
}
