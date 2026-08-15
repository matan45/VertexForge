#pragma once

#include <cstdint>
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
        // `viewportFocused` gates the cycle-grid shortcut, which is handled inside draw() so it
        // can never run while the panel is hidden. The panel itself carries
        // ImGuiWindowFlags_NoInputs, so the shortcut cannot be a widget - but a key check is
        // global and does not have to live outside this class to work.
        void draw(bool viewportFocused);

    private:
        // Registers the rebindable "Viewport.CycleStreamingGrid" action on first use and reports
        // whether it fired this frame. Lazy because the keybinding service is wired during editor
        // bootstrap and EventDispatcher::execute throws when no handler is registered yet.
        bool isCycleGridPressed();

        bool cycleGridActionRegistered = false;

        // Which grid the panel is showing. Clamped every frame against what the service reports,
        // so a world reload or a removed grid can never strand it out of range.
        uint8_t displayedGrid = 0;

        // Panel geometry. The grid is square and the cell size falls out of the ring width, so a
        // wide unload radius shrinks the cells rather than growing the panel.
        static constexpr float PANEL_SIZE = 208.0f;
        static constexpr float PANEL_MARGIN = 8.0f;

        static void drawGrid(const ::events::world::StreamingOverlaySnapshot& snapshot,
                             ImDrawList* drawList, const ImVec2& gridMin, float cellPx);
        static void drawRingsAndSources(const ::events::world::StreamingOverlaySnapshot& snapshot,
                                        ImDrawList* drawList, const ImVec2& gridMin, float cellPx);
        static void drawLegend();

        // VK-1600: eviction-pool occupancy bars. WHOLE-WORLD figures, unlike everything above
        // them in the panel, which is per displayed grid - the pools are shared across grids, so
        // the rows are labelled to say so. Skipped entirely when every budget is unlimited, which
        // is the default, so an unconfigured world's panel is unchanged.
        static void drawPoolBars();

        // State -> fill colour. Mirrors the Sector Grid tab's table (WorldSectorWindow.cpp) so the
        // two views never disagree about what a colour means.
        static ImU32 cellColor(const ::events::world::StreamingOverlayCell& cell);
    };
}
