#include "NavmeshWindow.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/NavmeshEvents.hpp"
#include "../../services/events/RenderEvents.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>

namespace windows
{
    void NavmeshWindow::show()
    {
        visible = true;
    }

    void NavmeshWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Navigation", &visible))
        {
            auto& dispatcher = events::EventDispatcher::instance();
            bool hasNavmesh = dispatcher.query(events::navmesh::HasNavmeshQuery{});

            if (hasNavmesh)
            {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Navmesh: Built");
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Navmesh: Not Built");
            }

            ImGui::Separator();

            drawBakeSettings();
            ImGui::Spacing();
            drawActions();
            ImGui::Spacing();
            drawDebugSection();
        }
        ImGui::End();
    }

    void NavmeshWindow::drawBakeSettings()
    {
        if (ImGui::CollapsingHeader("Bake Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            drawAgentSection();
            ImGui::Spacing();
            drawRegionSection();
            ImGui::Spacing();
            drawPolygonSection();
            ImGui::Spacing();
            drawFilterSection();

            ImGui::Unindent();
        }
    }

    void NavmeshWindow::drawAgentSection()
    {
        ImGui::Text("Agent");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);
        ImGui::Text("Radius");
        ImGui::DragFloat("##AgentRadius", &settings.agentRadius, 0.01f, 0.1f, 5.0f, "%.2f");

        ImGui::Text("Height");
        ImGui::DragFloat("##AgentHeight", &settings.agentHeight, 0.1f, 0.5f, 10.0f, "%.1f");

        ImGui::Text("Max Climb");
        ImGui::DragFloat("##AgentMaxClimb", &settings.agentMaxClimb, 0.01f, 0.0f, 5.0f, "%.2f");

        ImGui::Text("Max Slope");
        ImGui::DragFloat("##AgentMaxSlope", &settings.agentMaxSlope, 1.0f, 0.0f, 90.0f, "%.0f deg");
        ImGui::PopItemWidth();
    }

    void NavmeshWindow::drawRegionSection()
    {
        ImGui::Text("Voxelization");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);
        ImGui::Text("Cell Size");
        ImGui::DragFloat("##CellSize", &settings.cellSize, 0.01f, 0.05f, 2.0f, "%.2f");

        ImGui::Text("Cell Height");
        ImGui::DragFloat("##CellHeight", &settings.cellHeight, 0.01f, 0.05f, 2.0f, "%.2f");

        ImGui::Text("Min Region Size");
        ImGui::DragInt("##RegionMinSize", &settings.regionMinSize, 1, 0, 150);

        ImGui::Text("Region Merge Size");
        ImGui::DragInt("##RegionMergeSize", &settings.regionMergeSize, 1, 0, 150);
        ImGui::PopItemWidth();
    }

    void NavmeshWindow::drawPolygonSection()
    {
        ImGui::Text("Polygonization");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);
        ImGui::Text("Edge Max Length");
        ImGui::DragFloat("##EdgeMaxLen", &settings.edgeMaxLen, 0.1f, 0.0f, 50.0f, "%.1f");

        ImGui::Text("Edge Max Error");
        ImGui::DragFloat("##EdgeMaxError", &settings.edgeMaxError, 0.1f, 0.1f, 3.0f, "%.1f");

        ImGui::Text("Verts Per Poly");
        ImGui::DragInt("##VertsPerPoly", &settings.vertsPerPoly, 1, 3, 6);

        ImGui::Text("Detail Sample Distance");
        ImGui::DragFloat("##DetailSampleDist", &settings.detailSampleDist, 0.1f, 0.0f, 16.0f, "%.1f");

        ImGui::Text("Detail Sample Max Error");
        ImGui::DragFloat("##DetailSampleMaxError", &settings.detailSampleMaxError, 0.1f, 0.0f, 16.0f, "%.1f");
        ImGui::PopItemWidth();
    }

    void NavmeshWindow::drawFilterSection()
    {
        ImGui::Text("Input Filter");
        ImGui::Separator();

        ImGui::Checkbox("Include Terrain", &settings.includeTerrain);
        ImGui::Checkbox("Include Static Meshes", &settings.includeStaticMeshes);
        ImGui::Checkbox("Include Colliders", &settings.includeColliders);
    }

    void NavmeshWindow::drawActions()
    {
        if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            bool hasNavmesh = dispatcher.query(events::navmesh::HasNavmeshQuery{});

            if (ImGui::Button("Bake Navmesh", ImVec2(-1, 30)))
            {
                events::navmesh::BakeNavmeshCommand cmd;
                cmd.settings = settings;
                dispatcher.execute(cmd);
            }

            ImGui::BeginDisabled(!hasNavmesh);

            if (ImGui::Button("Clear Navmesh", ImVec2(-1, 0)))
            {
                events::navmesh::ClearNavmeshCommand cmd;
                dispatcher.execute(cmd);
            }

            ImGui::Spacing();

            if (ImGui::Button("Save Navmesh...", ImVec2(-1, 0)))
            {
                events::navmesh::SaveNavmeshCommand cmd;
                cmd.filePath = "navmesh.vfNavmesh";
                dispatcher.execute(cmd);
            }

            ImGui::EndDisabled();

            if (ImGui::Button("Load Navmesh...", ImVec2(-1, 0)))
            {
                events::navmesh::LoadNavmeshCommand cmd;
                cmd.filePath = "navmesh.vfNavmesh";
                dispatcher.execute(cmd);
            }

            ImGui::Unindent();
        }
    }

    void NavmeshWindow::drawDebugSection()
    {
        if (ImGui::CollapsingHeader("Debug Visualization", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            bool showNavmesh = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});

            if (ImGui::Checkbox("Show Navmesh Overlay", &showNavmesh))
            {
                events::render::SetShowNavmeshDebugCommand cmd;
                cmd.show = showNavmesh;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent();
        }
    }
}
