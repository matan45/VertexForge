#include "WaterDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/WaterEvents.hpp"
#include <imgui.h>
#include <filesystem>

namespace windows::details {

    bool WaterDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::water::HasWaterComponentQuery hasWaterQuery;
        hasWaterQuery.entity = handle;
        bool hasWater = dispatcher.query(hasWaterQuery);

        if (!hasWater)
            return false;

        events::water::GetWaterDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
            return true;

        const auto& data = *dataOpt;

        ImGui::PushID("WaterComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##WaterHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("Water");

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            ImGui::Text("Tile Size: %.1f units", data.worldTileSize);

            int gridWidth = data.gridMaxX - data.gridMinX + 1;
            int gridDepth = data.gridMaxZ - data.gridMinZ + 1;
            ImGui::Text("Grid: %d x %d tiles", gridWidth, gridDepth);
            ImGui::Text("Tile Count: %u", data.tileCount);
            ImGui::Text("Water Height: %.2f", data.defaultWaterHeight);
            ImGui::Text("Physics: %s", data.physicsEnabled ? "Enabled" : "Disabled");

            ImGui::Separator();
            ImGui::Text("Save");

            if (!data.savePath.empty())
            {
                std::filesystem::path p(data.savePath);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "File: %s", p.filename().string().c_str());
            }
            else
            {
                ImGui::TextDisabled("Not saved");
            }

            if (!data.savePath.empty())
            {
                if (ImGui::Button("Save"))
                {
                    startSave(handle, data.savePath);
                }
                ImGui::SameLine();
            }

            if (ImGui::Button("Save As..."))
            {
                startSaveAs(handle);
            }

            ImGui::SameLine();
            if (ImGui::Button("Load..."))
            {
                startLoad();
            }

            ImGui::Separator();
            ImGui::Text("Grid Expansion");

            ImGui::InputInt("Tile X", &pendingTileX);
            ImGui::InputInt("Tile Z", &pendingTileZ);

            if (ImGui::Button("Add Tile"))
            {
                events::water::AddWaterTileCommand cmd;
                cmd.waterEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    statusMessage = "Tile already exists or add failed";
                    statusFrameCounter = 180;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Tile"))
            {
                events::water::RemoveWaterTileCommand cmd;
                cmd.waterEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    statusMessage = "Tile not found or remove failed";
                    statusFrameCounter = 180;
                }
            }

            if (!statusMessage.empty())
            {
                statusFrameCounter--;
                if (statusFrameCounter <= 0)
                {
                    statusMessage.clear();
                }
                else
                {
                    bool isError = statusMessage.find("failed") != std::string::npos;
                    ImVec4 color = isError
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                        : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                    ImGui::TextColored(color, "%s", statusMessage.c_str());
                }
            }

            ImGui::Separator();

            // Streaming section
            if (ImGui::CollapsingHeader("Streaming"))
            {
                ImGui::Indent();

                events::water::IsWaterStreamingEnabledQuery enabledQuery;
                enabledQuery.waterEntity = handle;
                bool streamingEnabled = dispatcher.query(enabledQuery);

                if (ImGui::Checkbox("Enable Streaming", &streamingEnabled))
                {
                    events::water::SetWaterStreamingEnabledCommand cmd;
                    cmd.waterEntity = handle;
                    cmd.enabled = streamingEnabled;
                    dispatcher.execute(cmd);
                }

                if (streamingEnabled)
                {
                    if (!streamingConfigLoaded)
                    {
                        events::water::GetWaterStreamingConfigQuery cfgQuery;
                        cfgQuery.waterEntity = handle;
                        auto cfg = dispatcher.query(cfgQuery);
                        streamingLoadRadius = cfg.loadRadius;
                        streamingUnloadRadius = cfg.unloadRadius;
                        streamingMaxLoads = cfg.maxLoadsPerFrame;
                        streamingMaxUnloads = cfg.maxUnloadsPerFrame;
                        streamingConfigLoaded = true;
                    }

                    bool configChanged = false;
                    if (ImGui::SliderFloat("Load Radius", &streamingLoadRadius, 10.0f, 1000.0f, "%.0f"))
                        configChanged = true;
                    if (ImGui::SliderFloat("Unload Radius", &streamingUnloadRadius, 10.0f, 1500.0f, "%.0f"))
                    {
                        if (streamingUnloadRadius < streamingLoadRadius)
                            streamingUnloadRadius = streamingLoadRadius * 1.25f;
                        configChanged = true;
                    }
                    if (ImGui::SliderInt("Max Loads/Frame", &streamingMaxLoads, 1, 16))
                        configChanged = true;
                    if (ImGui::SliderInt("Max Unloads/Frame", &streamingMaxUnloads, 1, 16))
                        configChanged = true;

                    if (configChanged)
                    {
                        events::water::SetWaterStreamingConfigCommand cmd;
                        cmd.waterEntity = handle;
                        cmd.loadRadius = streamingLoadRadius;
                        cmd.unloadRadius = streamingUnloadRadius;
                        cmd.maxLoadsPerFrame = streamingMaxLoads;
                        cmd.maxUnloadsPerFrame = streamingMaxUnloads;
                        dispatcher.execute(cmd);
                    }

                    events::water::GetWaterStreamingStatsQuery statsQuery;
                    statsQuery.waterEntity = handle;
                    auto [loaded, total] = dispatcher.query(statsQuery);
                    ImGui::Text("Loaded: %u / %u tiles", loaded, total);

                    if (ImGui::Button("Load All Tiles"))
                    {
                        events::water::LoadAllWaterTilesCommand cmd;
                        cmd.waterEntity = handle;
                        dispatcher.execute(cmd);
                    }
                }
                else
                {
                    streamingConfigLoaded = false;
                }

                ImGui::Unindent();
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

    void WaterDrawer::startSave(services::EntityHandle handle, const std::string& path)
    {
        events::water::SaveWaterCommand cmd;
        cmd.waterEntity = handle;
        cmd.path = path;
        bool result = events::EventDispatcher::instance().execute(cmd);

        if (result)
        {
            statusMessage = "Saved successfully";
            statusFrameCounter = 180;
        }
        else
        {
            statusMessage = "Failed to save water";
            statusFrameCounter = 300;
        }
    }

    void WaterDrawer::startSaveAs(services::EntityHandle handle)
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Water (*.vfWater)", L"*.vfWater"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfWater");
        if (!path.empty())
        {
            startSave(handle, path);
        }
    }

    void WaterDrawer::startLoad()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Water (*.vfWater)", L"*.vfWater"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            events::water::LoadWaterCommand cmd;
            cmd.path = path;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

}
