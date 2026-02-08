#include "TerrainDrawer.hpp"
#include "../EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/TerrainEvents.hpp"
#include <imgui.h>
#include <filesystem>
#include <chrono>

namespace windows::details {

    bool TerrainDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::terrain::HasTerrainComponentQuery hasTerrainQuery;
        hasTerrainQuery.entity = handle;
        bool hasTerrain = dispatcher.query(hasTerrainQuery);

        if (!hasTerrain)
            return false;

        events::terrain::GetTerrainDataQuery terrainQuery;
        terrainQuery.entity = handle;
        auto terrainOpt = dispatcher.query(terrainQuery);

        if (!terrainOpt.has_value())
            return true;

        const auto& terrain = *terrainOpt;

        pollSaveResult(handle);

        ImGui::PushID("TerrainComponent");

        EntityDetailsPanel::pushComponentHeaderStyle();

        std::string headerLabel = terrain.saveDirty ? "Terrain *" : "Terrain";

        bool isOpen = ImGui::CollapsingHeader("##TerrainHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("%s", headerLabel.c_str());

        EntityDetailsPanel::popComponentHeaderStyle();

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            const char* resolutionNames[] = { "Low (33x33)", "Medium (65x65)", "High (129x129)", "Ultra (257x257)" };
            int resIndex = static_cast<int>(terrain.resolution);
            if (resIndex >= 0 && resIndex < 4)
            {
                ImGui::Text("Resolution: %s", resolutionNames[resIndex]);
            }

            ImGui::Text("Tile Size: %.1f units", terrain.worldTileSize);
            ImGui::Text("Height Range: %.1f to %.1f", terrain.minHeight, terrain.maxHeight);

            int gridWidth = terrain.gridMaxX - terrain.gridMinX + 1;
            int gridDepth = terrain.gridMaxZ - terrain.gridMinZ + 1;
            ImGui::Text("Grid: %d x %d tiles", gridWidth, gridDepth);
            ImGui::Text("Tile Count: %u", terrain.tileCount);

            ImGui::Separator();

            ImGui::Text("Active: %s", terrain.isActive ? "Yes" : "No");
            ImGui::Text("Dirty: %s", terrain.isDirty ? "Yes" : "No");

            ImGui::Separator();

            ImGui::Text("Active Tiles: %u", terrain.activeTileCount);
            ImGui::Text("Visible Tiles: %u", terrain.visibleTileCount);

            if (!terrain.heightmapPath.empty())
            {
                ImGui::Separator();
                ImGui::Text("Heightmap:");
                ImGui::TextWrapped("%s", terrain.heightmapPath.c_str());
            }

            // Save section
            ImGui::Separator();
            ImGui::Text("Save");

            if (!terrain.savePath.empty())
            {
                std::filesystem::path p(terrain.savePath);
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "File: %s", p.filename().string().c_str());
            }
            else
            {
                ImGui::TextDisabled("Not saved");
            }

            ImGui::BeginDisabled(isSaving);

            if (!terrain.savePath.empty())
            {
                if (ImGui::Button("Save"))
                {
                    startSave(handle, terrain.savePath);
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

            ImGui::EndDisabled();

            if (isSaving)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Saving...");
            }

            if (!saveStatusMessage.empty())
            {
                statusFrameCounter--;
                if (statusFrameCounter <= 0)
                {
                    saveStatusMessage.clear();
                }
                else
                {
                    bool isError = saveStatusMessage.find("Failed") != std::string::npos;
                    ImVec4 color = isError
                        ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                        : ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                    ImGui::TextColored(color, "%s", saveStatusMessage.c_str());
                }
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

    void TerrainDrawer::startSave(services::EntityHandle handle, const std::string& path)
    {
        isSaving = true;
        saveStatusMessage.clear();

        auto& dispatcher = events::EventDispatcher::instance();

        // Lock brush input before launching async save
        events::terrain::SetTerrainSaveLockCommand lockCmd;
        lockCmd.locked = true;
        dispatcher.execute(lockCmd);

        pendingSave = std::async(std::launch::async, [handle, path]()
        {
            events::terrain::SaveTerrainCommand cmd;
            cmd.terrainEntity = handle;
            cmd.path = path;
            return events::EventDispatcher::instance().execute(cmd);
        });
    }

    void TerrainDrawer::startSaveAs(services::EntityHandle handle)
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Terrain (*.vfTerrain)", L"*.vfTerrain"}
        };

        std::string path = fileDialog.saveFileDialog(fileTypes, L"vfTerrain");
        if (!path.empty())
        {
            startSave(handle, path);
        }
    }

    void TerrainDrawer::startLoad()
    {
        std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
            {L"VF Terrain (*.vfTerrain)", L"*.vfTerrain"}
        };

        std::string path = fileDialog.openFileDialog(fileTypes);
        if (!path.empty())
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::terrain::BeginTerrainLoadCommand cmd;
            cmd.path = path;
            dispatcher.execute(cmd);
        }
    }

    void TerrainDrawer::pollSaveResult(services::EntityHandle handle)
    {
        if (!isSaving || !pendingSave.valid())
            return;

        if (pendingSave.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            bool success = pendingSave.get();
            isSaving = false;

            // Unlock brush input
            auto& dispatcher = events::EventDispatcher::instance();
            events::terrain::SetTerrainSaveLockCommand lockCmd;
            lockCmd.locked = false;
            dispatcher.execute(lockCmd);

            if (success)
            {
                saveStatusMessage = "Saved successfully";
                statusFrameCounter = 180; // ~3 seconds at 60fps
            }
            else
            {
                saveStatusMessage = "Failed to save terrain";
                statusFrameCounter = 300; // ~5 seconds
            }
        }
    }

}
