#include "NavmeshWindow.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/navmesh/NavmeshEvents.hpp"
#include "../../services/events/render/RenderEvents.hpp"
#include "types/NavmeshTypes.hpp"
#include <imgui.h>

namespace windows
{
    NavmeshWindow::NavmeshWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        bakeCompleteToken = dispatcher.subscribe<events::navmesh::NavmeshBakeCompleteNotification>(
            [this](const events::navmesh::NavmeshBakeCompleteNotification&)
            {
                tileStatusDirty = true;
                auto& d = events::EventDispatcher::instance();
                bool showNavmesh = d.query(events::render::GetShowNavmeshDebugQuery{});
                if (showNavmesh)
                {
                    pushNavmeshDebugMesh();
                }
            });

        tileUpdatedToken = dispatcher.subscribe<events::navmesh::NavmeshTileUpdatedNotification>(
            [this](const events::navmesh::NavmeshTileUpdatedNotification&)
            {
                tileStatusDirty = true;
                auto& d = events::EventDispatcher::instance();
                bool showNavmesh = d.query(events::render::GetShowNavmeshDebugQuery{});
                if (showNavmesh)
                {
                    pushNavmeshDebugMesh();
                }
            });
    }

    NavmeshWindow::~NavmeshWindow()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (bakeCompleteToken.isValid())
        {
            dispatcher.unsubscribe(bakeCompleteToken);
        }
        if (tileUpdatedToken.isValid())
        {
            dispatcher.unsubscribe(tileUpdatedToken);
        }
    }

    void NavmeshWindow::pushNavmeshDebugMesh()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        bool hasNavmesh = dispatcher.query(events::navmesh::HasNavmeshQuery{});
        if (!hasNavmesh)
        {
            events::render::ClearNavmeshDebugMeshCommand clearCmd;
            dispatcher.execute(clearCmd);
            return;
        }

        events::navmesh::GetNavmeshDebugMeshQuery meshQuery;
        auto debugMesh = dispatcher.query(meshQuery);

        if (!debugMesh.vertices.empty() && !debugMesh.indices.empty())
        {
            events::render::UpdateNavmeshDebugMeshCommand updateCmd;
            updateCmd.vertices = std::move(debugMesh.vertices);
            updateCmd.indices = std::move(debugMesh.indices);
            dispatcher.execute(updateCmd);
        }
    }

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
            drawTileStatus();
            ImGui::Spacing();
            drawStreamingConfig();

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
            ImGui::Spacing();
            drawAreaCosts();
            ImGui::Spacing();
            drawLodSettings();

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

    void NavmeshWindow::drawAreaCosts()
    {
        ImGui::Text("Area Costs");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);
        // Show user-facing area costs (Road through Hazard, indices 5-9)
        for (int i = types::NAVMESH_AREA_ROAD; i <= types::NAVMESH_AREA_HAZARD; ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "%s##AreaCost%d", types::getNavmeshAreaName(static_cast<uint8_t>(i)), i);
            ImGui::DragFloat(label, &settings.areaCosts[i], 0.1f, 0.1f, 100.0f, "%.1f");
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Checkbox("Auto-generate Drop Links", &settings.autoGenerateDropLinks);
        if (settings.autoGenerateDropLinks)
        {
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat("##AutoDropMax", &settings.autoDropMaxHeight, 0.1f, 0.5f, 50.0f, "Max Height: %.1f");
            ImGui::DragFloat("##AutoDropMin", &settings.autoDropMinHeight, 0.1f, 0.1f, settings.autoDropMaxHeight, "Min Height: %.1f");
            ImGui::PopItemWidth();
        }

        ImGui::Spacing();
        ImGui::Text("Pathfinding");
        ImGui::Separator();
        ImGui::PushItemWidth(-1);
        ImGui::DragFloat("##HierarchicalThreshold", &settings.hierarchicalPathThreshold, 1.0f, 0.0f, 10000.0f, "Hierarchical Threshold: %.0f");
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Paths longer than this distance use tile-level hierarchical A* for better performance");
    }

    void NavmeshWindow::drawLodSettings()
    {
        ImGui::Text("LOD");
        ImGui::Separator();

        ImGui::PushItemWidth(-1);

        int lodCount = static_cast<int>(settings.lodConfig.lodCount);
        ImGui::Text("LOD Count");
        if (ImGui::SliderInt("##LodCount", &lodCount, 1, 3))
        {
            settings.lodConfig.lodCount = static_cast<uint8_t>(lodCount);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("1 = no LOD, 2-3 = multi-resolution navmesh tiles");

        if (settings.lodConfig.lodCount > 1)
        {
            for (int i = 0; i < settings.lodConfig.lodCount; ++i)
            {
                char label[64];
                snprintf(label, sizeof(label), "LOD %d Distance##LodDist%d", i, i);
                ImGui::DragFloat(label, &settings.lodConfig.lodDistances[i], 1.0f, 0.0f, 4096.0f, "%.0f");
            }
        }

        ImGui::PopItemWidth();
    }

    void NavmeshWindow::drawActions()
    {
        if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            auto bakeProgress = dispatcher.query(events::navmesh::GetBakeProgressQuery{});
            bool isBaking = bakeProgress.status == types::NavmeshBakeStatus::Collecting ||
                            bakeProgress.status == types::NavmeshBakeStatus::Voxelizing ||
                            bakeProgress.status == types::NavmeshBakeStatus::Building;

            if (isBaking)
            {
                ImGui::ProgressBar(bakeProgress.progress, ImVec2(-1, 0));
                if (!bakeProgress.currentStage.empty())
                {
                    ImGui::TextWrapped("%s", bakeProgress.currentStage.c_str());
                }
            }

            bool hasNavmesh = dispatcher.query(events::navmesh::HasNavmeshQuery{});

            ImGui::BeginDisabled(isBaking);

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
                std::string savePath = fileDialog.selectFolderDialog();
                if (!savePath.empty())
                {
                    events::navmesh::SaveNavmeshTiledCommand cmd;
                    cmd.directory = savePath;
                    dispatcher.execute(cmd);
                }
            }

            ImGui::EndDisabled();

            if (ImGui::Button("Load Navmesh...", ImVec2(-1, 0)))
            {
                std::string loadPath = fileDialog.selectFolderDialog();
                if (!loadPath.empty())
                {
                    events::navmesh::LoadNavmeshTiledCommand cmd;
                    cmd.directory = loadPath;
                    dispatcher.execute(cmd);
                }
            }

            ImGui::EndDisabled();

            ImGui::Unindent();
        }
    }

    void NavmeshWindow::drawTileStatus()
    {
        if (ImGui::CollapsingHeader("Tile Status"))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            if (tileStatusDirty)
            {
                cachedTileStatuses = dispatcher.query(events::navmesh::GetNavmeshTileStatusQuery{});
                tileStatusDirty = false;
            }
            const auto& tileStatuses = cachedTileStatuses;

            if (tileStatuses.empty())
            {
                ImGui::TextDisabled("No tiles");
            }
            else
            {
                int loadedCount = 0;
                int bakedCount = 0;
                int dirtyCount = 0;

                for (const auto& info : tileStatuses)
                {
                    switch (info.status)
                    {
                    case events::navmesh::NavmeshTileStatus::Loaded: loadedCount++; break;
                    case events::navmesh::NavmeshTileStatus::Baked: bakedCount++; break;
                    case events::navmesh::NavmeshTileStatus::Dirty: dirtyCount++; break;
                    default: break;
                    }
                }

                ImGui::Text("Total: %d | Loaded: %d | Baked: %d | Dirty: %d",
                    static_cast<int>(tileStatuses.size()), loadedCount, bakedCount, dirtyCount);

                if (ImGui::BeginChild("TileGrid", ImVec2(0, 120), true))
                {
                    for (const auto& info : tileStatuses)
                    {
                        ImVec4 color;
                        const char* label;
                        switch (info.status)
                        {
                        case events::navmesh::NavmeshTileStatus::Loaded:
                            color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
                            label = "Loaded";
                            break;
                        case events::navmesh::NavmeshTileStatus::Dirty:
                            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                            label = "Dirty";
                            break;
                        case events::navmesh::NavmeshTileStatus::Baking:
                            color = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);
                            label = "Baking";
                            break;
                        case events::navmesh::NavmeshTileStatus::Baked:
                            color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                            label = "Baked";
                            break;
                        default:
                            color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                            label = "Not Baked";
                            break;
                        }

                        ImGui::TextColored(color, "(%d, %d) %s", info.coord.x, info.coord.z, label);

                        ImGui::SameLine();
                        char btnLabel[32];
                        snprintf(btnLabel, sizeof(btnLabel), "Bake##%d_%d", info.coord.x, info.coord.z);
                        if (ImGui::SmallButton(btnLabel))
                        {
                            events::navmesh::BakeTileCommand cmd;
                            cmd.tileX = info.coord.x;
                            cmd.tileZ = info.coord.z;
                            dispatcher.execute(cmd);
                        }
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Unindent();
        }
    }

    void NavmeshWindow::drawStreamingConfig()
    {
        if (ImGui::CollapsingHeader("Streaming"))
        {
            ImGui::Indent();

            auto& dispatcher = events::EventDispatcher::instance();
            bool enabled = dispatcher.query(events::navmesh::IsNavmeshStreamingEnabledQuery{});
            streamingConfig = dispatcher.query(events::navmesh::GetNavmeshStreamingConfigQuery{});

            if (ImGui::Checkbox("Enable Streaming", &enabled))
            {
                events::navmesh::SetNavmeshStreamingEnabledCommand cmd;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);
            }

            ImGui::PushItemWidth(-1);

            bool changed = false;
            ImGui::Text("Load Radius");
            changed |= ImGui::DragFloat("##LoadRadius", &streamingConfig.loadRadius, 1.0f, 32.0f, 2048.0f, "%.0f");

            ImGui::Text("Unload Radius");
            changed |= ImGui::DragFloat("##UnloadRadius", &streamingConfig.unloadRadius, 1.0f, 32.0f, 2048.0f, "%.0f");

            ImGui::Text("Max Loads/Frame");
            changed |= ImGui::DragInt("##MaxLoads", &streamingConfig.maxLoadsPerFrame, 1, 1, 16);

            ImGui::Text("Max Unloads/Frame");
            changed |= ImGui::DragInt("##MaxUnloads", &streamingConfig.maxUnloadsPerFrame, 1, 1, 16);

            ImGui::PopItemWidth();

            if (changed)
            {
                events::navmesh::SetNavmeshStreamingConfigCommand cmd;
                cmd.config = streamingConfig;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent();
        }
    }

}
