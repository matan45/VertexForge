#include "ViewPortStreamingOverlay.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include <algorithm>
#include <cstdio>

namespace windows
{
    namespace
    {
        // Same hues the Sector Grid tab uses, as packed ImU32 so the draw loop does no conversion.
        constexpr ImU32 COL_MISSING    = IM_COL32(38, 38, 38, 200);  // no sector at this coord
        constexpr ImU32 COL_UNLOADED   = IM_COL32(102, 102, 102, 220);
        constexpr ImU32 COL_LOADING    = IM_COL32(230, 230, 51, 255);
        constexpr ImU32 COL_LOADED     = IM_COL32(51, 204, 51, 255);
        constexpr ImU32 COL_UNLOADING  = IM_COL32(230, 77, 77, 255);
        constexpr ImU32 COL_PREFETCHNG = IM_COL32(51, 153, 179, 255);
        constexpr ImU32 COL_PREFETCHED = IM_COL32(51, 204, 230, 255);

        constexpr ImU32 COL_GRID       = IM_COL32(0, 0, 0, 90);
        constexpr ImU32 COL_DIRTY      = IM_COL32(255, 140, 40, 255);
        constexpr ImU32 COL_HLOD       = IM_COL32(200, 120, 255, 255);
        constexpr ImU32 COL_CENTER     = IM_COL32(80, 140, 255, 255);

        constexpr ImU32 COL_RING_LOAD     = IM_COL32(90, 220, 90, 200);
        constexpr ImU32 COL_RING_PREFETCH = IM_COL32(70, 210, 235, 170);
        constexpr ImU32 COL_RING_UNLOAD   = IM_COL32(235, 110, 110, 170);
        constexpr ImU32 COL_SRC_CAMERA    = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 COL_SRC_SCRIPT    = IM_COL32(255, 210, 90, 255);
    }

    ImU32 ViewPortStreamingOverlay::cellColor(const ::events::world::StreamingOverlayCell& cell)
    {
        if (!cell.exists)
            return COL_MISSING;

        switch (cell.state)
        {
        case ::world::SectorState::Loaded:      return COL_LOADED;
        case ::world::SectorState::Loading:     return COL_LOADING;
        case ::world::SectorState::Unloading:   return COL_UNLOADING;
        case ::world::SectorState::Prefetching: return COL_PREFETCHNG;
        case ::world::SectorState::Prefetched:  return COL_PREFETCHED;
        default:                                return COL_UNLOADED;
        }
    }

    void ViewPortStreamingOverlay::draw()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (!dispatcher.query(events::world::GetStreamingOverlayVisibleQuery{}))
            return;

        // Deliberately no play-mode early-out (unlike ViewPortOverlay): the ticket requires this to
        // work in play-in-editor, which is where streaming is most interesting.
        //
        // VK-1599: one grid per snapshot. The service refills the SAME borrowed buffers on every
        // query and the contract is single-consumer, so asking for all grids at once is exactly the
        // aliasing hazard GetStreamingOverlaySnapshotQuery's header forbids. The panel steps
        // through them instead, on the viewport's own cycle key.
        events::world::GetStreamingOverlaySnapshotQuery snapshotQuery;
        snapshotQuery.gridIndex = displayedGrid;
        const auto snapshot = dispatcher.query(snapshotQuery);

        // Clamp against what the service actually reported: a world reload or a Remove Grid can
        // shrink the list under a selection made a moment ago, and the service already folded an
        // out-of-range request onto the primary grid.
        displayedGrid = snapshot.gridIndex;

        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();

        // Bottom-left: ##ViewportOverlay owns the top-left corner and ##ViewModeOverlay the
        // top-right, so this is the only free anchor that never overlaps either.
        // The panel plus its header and footer lines. AlwaysAutoResize sizes the window itself; this
        // only has to place its top edge so the whole thing lands above the viewport's bottom edge.
        const float estimatedHeight = PANEL_SIZE + ImGui::GetFrameHeightWithSpacing() * 2.0f;
        const ImVec2 overlayPos(
            windowPos.x + contentMin.x + PANEL_MARGIN,
            windowPos.y + contentMax.y - PANEL_MARGIN - estimatedHeight);

        constexpr ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove
            // MANDATORY, not cosmetic. This panel is ~208px of pure readout with no interactive
            // widget in it, but as an ordinary ImGui window it would still steal hover from the
            // ViewPort. ImGui ignores NoInputs windows in IsWindowHovered() (imgui.h), and the
            // ViewPort gates all of its terrain tooling on exactly that: without this flag the
            // panel becomes a brush dead zone (ViewPort.cpp updateBrushCursors) and - much worse -
            // dragging a sculpt stroke across it trips the VK-1615 "dragged off the viewport" rail
            // in handleSculptBrush and FINALIZES the stroke mid-drag.
            | ImGuiWindowFlags_NoInputs;

        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##StreamingOverlay", nullptr, overlayFlags))
        {
            if (!snapshot.valid)
            {
                ImGui::TextDisabled("Streaming: no world loaded");
            }
            else
            {
                char header[192];
                if (snapshot.gridCount > 1)
                {
                    // The grid is only named when there is more than one - a single-grid world's
                    // header would otherwise gain a "Default 1/1" that says nothing.
                    std::snprintf(header, sizeof(header),
                                  "Streaming  %s %u/%u  (%d, %d)  r=%d%s",
                                  snapshot.gridName.c_str(),
                                  static_cast<unsigned>(snapshot.gridIndex + 1),
                                  static_cast<unsigned>(snapshot.gridCount),
                                  snapshot.center.x, snapshot.center.z, snapshot.radius,
                                  snapshot.radiusClamped ? " (clamped)" : "");
                }
                else
                {
                    std::snprintf(header, sizeof(header), "Streaming  (%d, %d)  r=%d%s",
                                  snapshot.center.x, snapshot.center.z, snapshot.radius,
                                  snapshot.radiusClamped ? " (clamped)" : "");
                }
                ImGui::TextUnformatted(header);

                if (snapshot.overrideActive)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "OVERRIDE");
                }
                if (snapshot.paused)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.25f, 1.0f), "PAUSED");
                }
                if (snapshot.burstFramesRemaining > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "BURST %d",
                                       snapshot.burstFramesRemaining);
                }

                const int32_t gridWidth = snapshot.radius * 2 + 1;
                const float cellPx = PANEL_SIZE / static_cast<float>(gridWidth);

                const ImVec2 gridMin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(PANEL_SIZE, PANEL_SIZE));
                ImGui::SameLine();
                ImGui::BeginGroup();
                drawLegend();
                ImGui::EndGroup();

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(gridMin,
                                       ImVec2(gridMin.x + PANEL_SIZE, gridMin.y + PANEL_SIZE),
                                       true);
                drawGrid(snapshot, drawList, gridMin, cellPx);
                drawRingsAndSources(snapshot, drawList, gridMin, cellPx);
                drawList->PopClipRect();

                char radii[128];
                std::snprintf(radii, sizeof(radii), "load %.1f  prefetch %.1f  unload %.1f",
                              static_cast<double>(snapshot.loadRadius),
                              static_cast<double>(snapshot.prefetchRadius),
                              static_cast<double>(snapshot.unloadRadius));
                ImGui::TextDisabled("%s", radii);
            }
        }
        ImGui::End();

        ImGui::PopStyleVar();
    }

    void ViewPortStreamingOverlay::drawGrid(const ::events::world::StreamingOverlaySnapshot& snapshot,
                                            ImDrawList* drawList, const ImVec2& gridMin, float cellPx)
    {
        const int32_t gridWidth = snapshot.radius * 2 + 1;

        // Cells arrive row-major with z DESCENDING, so index order is already top-to-bottom.
        for (uint32_t i = 0; i < snapshot.cellCount; ++i)
        {
            const auto& cell = snapshot.cells[i];
            const int32_t col = static_cast<int32_t>(i) % gridWidth;
            const int32_t row = static_cast<int32_t>(i) / gridWidth;

            const ImVec2 min(gridMin.x + static_cast<float>(col) * cellPx,
                             gridMin.y + static_cast<float>(row) * cellPx);
            const ImVec2 max(min.x + cellPx, min.y + cellPx);

            drawList->AddRectFilled(min, max, cellColor(cell));

            if (cellPx >= 4.0f)
                drawList->AddRect(min, max, COL_GRID);

            // A dirty sector is pinned against auto-unload, which is exactly the thing you want to
            // spot when a ring refuses to shrink.
            if (cell.dirty)
                drawList->AddRect(min, max, COL_DIRTY, 0.0f, 0, 2.0f);

            // HLOD coverage as a corner wedge rather than a fill, so the sector's own state stays
            // readable underneath it.
            if (cell.hlodVisible && cellPx >= 5.0f)
            {
                const float tick = cellPx * 0.4f;
                drawList->AddTriangleFilled(ImVec2(max.x - tick, min.y),
                                            ImVec2(max.x, min.y),
                                            ImVec2(max.x, min.y + tick), COL_HLOD);
            }

            if (cell.coord.x == snapshot.center.x && cell.coord.z == snapshot.center.z)
                drawList->AddRect(min, max, COL_CENTER, 0.0f, 0, 2.0f);
        }
    }

    void ViewPortStreamingOverlay::drawRingsAndSources(
        const ::events::world::StreamingOverlaySnapshot& snapshot,
        ImDrawList* drawList, const ImVec2& gridMin, float cellPx)
    {
        if (!(snapshot.sectorWorldSize > 0.0f))
            return;

        // World XZ -> panel pixels. The grid's top-left cell is (center.x - radius, center.z +
        // radius) and z runs DOWN the panel, so the z axis is negated.
        const float pxPerWorld = cellPx / snapshot.sectorWorldSize;
        const float originWorldX =
            static_cast<float>(snapshot.center.x - snapshot.radius) * snapshot.sectorWorldSize;
        const float originWorldZ =
            static_cast<float>(snapshot.center.z + snapshot.radius + 1) * snapshot.sectorWorldSize;

        auto toPanel = [&](float worldX, float worldZ)
        {
            return ImVec2(gridMin.x + (worldX - originWorldX) * pxPerWorld,
                          gridMin.y + (originWorldZ - worldZ) * pxPerWorld);
        };

        for (uint32_t i = 0; i < snapshot.sourceCount; ++i)
        {
            const auto& source = snapshot.sources[i];
            const ImVec2 center = toPanel(source.position.x, source.position.z);
            const float mult = source.radiusMultiplier;

            // A prefetch-only source never activates anything, so drawing it an activate ring would
            // be a lie about what it is doing.
            if (source.targetState == ::world::SectorTargetState::Activated)
            {
                drawList->AddCircle(center, snapshot.loadRadius * snapshot.sectorWorldSize * mult
                                                * pxPerWorld, COL_RING_LOAD, 0, 1.5f);
            }
            if (snapshot.prefetchRadius > snapshot.loadRadius)
            {
                drawList->AddCircle(center, snapshot.prefetchRadius * snapshot.sectorWorldSize * mult
                                                * pxPerWorld, COL_RING_PREFETCH, 0, 1.0f);
            }
            drawList->AddCircle(center, snapshot.unloadRadius * snapshot.sectorWorldSize * mult
                                            * pxPerWorld, COL_RING_UNLOAD, 0, 1.0f);

            const ImU32 marker = source.isCamera ? COL_SRC_CAMERA : COL_SRC_SCRIPT;
            drawList->AddCircleFilled(center, source.isCamera ? 3.5f : 2.5f, marker);
            drawList->AddCircle(center, source.isCamera ? 3.5f : 2.5f, IM_COL32(0, 0, 0, 200));
        }
    }

    void ViewPortStreamingOverlay::drawLegend()
    {
        auto swatch = [](ImU32 color, const char* label)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float size = ImGui::GetTextLineHeight();
            drawList->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), color);
            drawList->AddRect(pos, ImVec2(pos.x + size, pos.y + size), COL_GRID);
            ImGui::Dummy(ImVec2(size, size));
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        };

        swatch(COL_LOADED, "Loaded");
        swatch(COL_LOADING, "Loading");
        swatch(COL_PREFETCHED, "Prefetched");
        swatch(COL_PREFETCHNG, "Prefetching");
        swatch(COL_UNLOADING, "Unloading");
        swatch(COL_UNLOADED, "Unloaded");
        swatch(COL_MISSING, "No sector");
        swatch(COL_DIRTY, "Dirty (pinned)");
        swatch(COL_HLOD, "HLOD proxy");
    }
}
