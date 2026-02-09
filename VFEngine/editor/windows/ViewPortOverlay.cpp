#include "ViewPortOverlay.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "events/SculptModeEvents.hpp"
#include "events/PaintModeEvents.hpp"
#include "events/SceneEvents.hpp"
#include "events/TerrainEvents.hpp"
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

        ImGui::SetNextWindowPos(overlayPos);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

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

                ImGui::BeginDisabled(isSculptMode || isPaintMode);

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

                // Sculpt mode toggle - enabled when terrain is selected or already sculpting
                bool canSculpt = isSculptMode;
                if (!canSculpt)
                {
                    auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
                    if (selectedEntity.has_value())
                    {
                        events::terrain::HasTerrainComponentQuery terrainQuery;
                        terrainQuery.entity = *selectedEntity;
                        canSculpt = dispatcher.query(terrainQuery);

                        if (!canSculpt)
                        {
                            events::terrain::HasTerrainTileComponentQuery tileQuery;
                            tileQuery.entity = *selectedEntity;
                            canSculpt = dispatcher.query(tileQuery);
                        }
                    }
                }

                ImGui::BeginDisabled(!canSculpt);
                if (iconButton(ViewportIcon::Sculpt, isSculptMode, isSculptMode ? "Exit Sculpt Mode" : "Enter Sculpt Mode"))
                {
                    events::sculpt::SetSculptModeActiveCommand cmd;
                    cmd.active = !isSculptMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                // Paint mode toggle - enabled when terrain is selected or already painting
                bool canPaint = isPaintMode;
                if (!canPaint)
                {
                    auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
                    if (selectedEntity.has_value())
                    {
                        events::terrain::HasTerrainComponentQuery terrainQuery2;
                        terrainQuery2.entity = *selectedEntity;
                        canPaint = dispatcher.query(terrainQuery2);

                        if (!canPaint)
                        {
                            events::terrain::HasTerrainTileComponentQuery tileQuery2;
                            tileQuery2.entity = *selectedEntity;
                            canPaint = dispatcher.query(tileQuery2);
                        }
                    }
                }

                ImGui::BeginDisabled(!canPaint);
                if (iconButton(ViewportIcon::Paint, isPaintMode, isPaintMode ? "Exit Paint Mode" : "Enter Paint Mode"))
                {
                    events::paint::SetPaintModeActiveCommand cmd;
                    cmd.active = !isPaintMode;
                    dispatcher.execute(cmd);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::End();

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
                "Color",        // 0: Normal rendering
                "Meshlet",      // 1: Meshlet visualization
                "LOD",          // 2: LOD level colors
                "Mipmap",       // 3: Texture mip level
                "Cluster",      // 4: Light cluster visualization
                "Depth",        // 5: Depth visualization
                "Shadow",       // 6: Shadow visualization
                "Terrain Tile", // 7: Terrain tile visualization (color per tile)
                "Terrain UV"    // 8: Terrain world UV visualization
            };
            ImGui::SetNextItemWidth(dropdownWidth);
            if (ImGui::Combo("##ViewMode", &currentViewMode, viewModeLabels, 9))
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

        ImGui::PopStyleVar(2);
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
