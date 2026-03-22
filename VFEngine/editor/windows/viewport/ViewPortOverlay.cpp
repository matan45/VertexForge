#include "ViewPortOverlay.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/HoleModeEvents.hpp"
#include "events/terrain/CaveModeEvents.hpp"
#include "events/vegetation/VegetationBrushEvents.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include <imgui.h>

namespace windows
{
    ViewPortOverlay::~ViewPortOverlay()
    {
        if (iconAtlas.isValid())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::render::ReleaseEditorTextureCommand cmd;
            cmd.handle = iconAtlas.imguiDescriptorSet;
            dispatcher.execute(cmd);
        }
    }

    void ViewPortOverlay::draw(ViewPortGizmo& gizmo)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

        if (isPlayMode)
        {
            return;
        }

        if (!iconsLoaded)
        {
            loadIconAtlas();
        }

        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
        ImVec2 overlayPos = ImVec2(windowPos.x + contentMin.x + 8.0f,
                                   windowPos.y + contentMin.y + 8.0f);

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        drawToolbar(gizmo, overlayFlags, overlayPos);
        drawViewModeDropdown(overlayFlags, windowPos, contentMin);

        ImGui::PopStyleVar(2);
    }

    void ViewPortOverlay::drawToolbar(ViewPortGizmo& gizmo, ImGuiWindowFlags overlayFlags, const ImVec2& overlayPos)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowBgAlpha(0.0f);

        if (ImGui::Begin("##ViewportOverlay", nullptr, overlayFlags))
        {
            bool currentGridState = dispatcher.query(events::render::GetShowGridQuery{});

            if (iconAtlas.isValid())
            {
                if (iconButton(ViewportIcon::Grid, currentGridState, "Toggle 3D grid overlay"))
                {
                    events::render::SetShowGridCommand cmd;
                    cmd.show = !currentGridState;
                    dispatcher.execute(cmd);
                }

                ImGui::SameLine();
                bool isWorldMode = (gizmo.getMode() == ImGuizmo::WORLD);
                if (iconButton(ViewportIcon::World, isWorldMode, isWorldMode ? "World space" : "Local space"))
                {
                    gizmo.toggleMode();
                }

                ImGui::SameLine();

                bool isSculptMode = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
                bool isPaintMode = dispatcher.query(events::paint::IsPaintModeActiveQuery{});
                bool isHoleMode = dispatcher.query(events::hole::IsHoleModeActiveQuery{});
                bool isCaveMode = dispatcher.query(events::cave::IsCaveModeActiveQuery{});
                bool isVegBrushMode = dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{});
                bool isMeshBrushMode = dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{});
                ImGui::BeginDisabled(isSculptMode || isPaintMode || isHoleMode || isCaveMode || isVegBrushMode || isMeshBrushMode);

                if (iconButton(ViewportIcon::Rotate, gizmo.getOperation() == GizmoOperation::Rotate, "Rotate tool"))
                {
                    gizmo.toggleOperation(GizmoOperation::Rotate);
                }

                ImGui::SameLine();

                if (iconButton(ViewportIcon::Scale, gizmo.getOperation() == GizmoOperation::Scale, "Scale tool"))
                {
                    gizmo.toggleOperation(GizmoOperation::Scale);
                }

                ImGui::SameLine();

                if (iconButton(ViewportIcon::Translate, gizmo.getOperation() == GizmoOperation::Translate, "Move tool"))
                {
                    gizmo.toggleOperation(GizmoOperation::Translate);
                }

                ImGui::EndDisabled();

                bool canUseTerrain = isTerrainSelected();
                auto svtBakeStatus = dispatcher.query(events::terrain::PollBakeTerrainSVTQuery{});
                bool bakeBlocked = svtBakeStatus.active;

                ImGui::BeginDisabled(bakeBlocked || (!isSculptMode && !canUseTerrain));
                if (iconButton(ViewportIcon::Sculpt, isSculptMode, isSculptMode ? "Exit Sculpt Mode" : "Enter Sculpt Mode"))
                {
                    events::sculpt::SetSculptModeActiveCommand cmd;
                    cmd.active = !isSculptMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                ImGui::BeginDisabled(bakeBlocked || (!isPaintMode && !canUseTerrain));
                if (iconButton(ViewportIcon::Paint, isPaintMode, isPaintMode ? "Exit Paint Mode" : "Enter Paint Mode"))
                {
                    events::paint::SetPaintModeActiveCommand cmd;
                    cmd.active = !isPaintMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                ImGui::BeginDisabled(bakeBlocked || (!isHoleMode && !canUseTerrain));
                if (iconButton(ViewportIcon::Hole, isHoleMode, isHoleMode ? "Exit Hole Mode" : "Enter Hole Mode"))
                {
                    events::hole::SetHoleModeActiveCommand cmd;
                    cmd.active = !isHoleMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                ImGui::BeginDisabled(bakeBlocked || (!isCaveMode && !canUseTerrain));
                if (iconButton(ViewportIcon::Cave, isCaveMode, isCaveMode ? "Exit Cave Mode" : "Enter Cave Mode"))
                {
                    events::cave::SetCaveModeActiveCommand cmd;
                    cmd.active = !isCaveMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::BeginDisabled(bakeBlocked || (!isVegBrushMode && !canUseTerrain));
                if (iconButton(ViewportIcon::Vegetation, isVegBrushMode, isVegBrushMode ? "Exit Vegetation Brush" : "Enter Vegetation Brush"))
                {
                    events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
                    cmd.active = !isVegBrushMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                ImGui::BeginDisabled(bakeBlocked || (!isMeshBrushMode && !canUseTerrain));
                if (iconButton(ViewportIcon::MeshBrush, isMeshBrushMode, isMeshBrushMode ? "Exit Mesh Brush" : "Enter Mesh Brush"))
                {
                    events::meshBrush::SetMeshBrushModeActiveCommand cmd;
                    cmd.active = !isMeshBrushMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::End();
    }

    void ViewPortOverlay::drawViewModeDropdown(ImGuiWindowFlags overlayFlags, const ImVec2& windowPos,
                                               const ImVec2& contentMin)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
        float dropdownWidth = 95.0f;
        ImVec2 dropdownPos = ImVec2(
            windowPos.x + contentMax.x - dropdownWidth - 8.0f,
            windowPos.y + contentMin.y + 8.0f
        );

        ImGui::SetNextWindowPos(dropdownPos);
        ImGui::SetNextWindowBgAlpha(0.75f);

        if (ImGui::Begin("##ViewModeOverlay", nullptr, overlayFlags))
        {
            currentViewMode = static_cast<int>(dispatcher.query(events::render::GetViewModeQuery{}));

            const char* viewModeLabels[] = {
                "Color",
                "Meshlet",
                "LOD",
                "Mipmap",
                "Cluster",
                "Depth",
                "Shadow",
                "Terrain Tile",
                "Terrain UV",
                "Weight Map"
            };
            ImGui::SetNextItemWidth(dropdownWidth);
            if (ImGui::Combo("##ViewMode", &currentViewMode, viewModeLabels, 10))
            {
                events::render::SetViewModeCommand cmd;
                cmd.mode = static_cast<uint32_t>(currentViewMode);
                dispatcher.execute(cmd);
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Viewport visualization mode");
            }
        }
        ImGui::End();
    }

    bool ViewPortOverlay::isTerrainSelected() const
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value())
            return false;

        events::terrain::HasTerrainComponentQuery terrainQuery;
        terrainQuery.entity = *selectedEntity;
        if (dispatcher.query(terrainQuery))
            return true;

        events::terrain::HasTerrainTileComponentQuery tileQuery;
        tileQuery.entity = *selectedEntity;
        return dispatcher.query(tileQuery);
    }

    void ViewPortOverlay::loadIconAtlas()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::render::LoadEditorTextureCommand cmd;
        cmd.path = "../../resources/editor/viewPortAtlasIcons.vfImage";
        iconAtlas = dispatcher.execute(cmd);

        iconsLoaded = true;
    }

    std::pair<glm::vec2, glm::vec2> ViewPortOverlay::getIconUV(ViewportIcon icon) const
    {
        uint32_t index = static_cast<uint32_t>(icon);
        uint32_t maxIndex = ATLAS_COLUMNS * ATLAS_ROWS;

        if (index >= maxIndex)
        {
            index = 0;
        }

        float colSize = 1.0f / static_cast<float>(ATLAS_COLUMNS);
        float rowSize = 1.0f / static_cast<float>(ATLAS_ROWS);

        float col = static_cast<float>(index % ATLAS_COLUMNS);
        float row = static_cast<float>(index / ATLAS_COLUMNS);

        glm::vec2 uv0(col * colSize, row * rowSize);
        glm::vec2 uv1((col + 1.0f) * colSize, (row + 1.0f) * rowSize);

        return {uv0, uv1};
    }

    bool ViewPortOverlay::iconButton(ViewportIcon icon, bool isActive, const char* tooltip)
    {
        auto [uv0, uv1] = getIconUV(icon);

        ImGui::PushID(static_cast<int>(icon));

        ImVec4 bgColor = isActive ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        ImVec4 tintColor = isActive ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(bgColor.x + 0.1f, bgColor.y + 0.1f, bgColor.z + 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(bgColor.x + 0.2f, bgColor.y + 0.2f, bgColor.z + 0.2f, 1.0f));

        bool clicked = ImGui::ImageButton(
            "##iconBtn",
            iconAtlas.imguiDescriptorSet,
            ImVec2(ICON_SIZE, ICON_SIZE),
            ImVec2(uv0.x, uv0.y),
            ImVec2(uv1.x, uv1.y),
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
            tintColor
        );

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", tooltip);
        }

        ImGui::PopID();

        return clicked;
    }
}
