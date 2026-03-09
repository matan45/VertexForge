#include "TerrainDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "types/PhysicsTypes.hpp"
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

            ImGui::Separator();
            ImGui::Text("Grid Expansion");

            ImGui::InputInt("Tile X", &pendingTileX);
            ImGui::InputInt("Tile Z", &pendingTileZ);

            if (ImGui::Button("Add Tile"))
            {
                events::terrain::AddTerrainTileCommand cmd;
                cmd.terrainEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    saveStatusMessage = "Tile already exists or add failed";
                    statusFrameCounter = 180;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Tile"))
            {
                events::terrain::RemoveTerrainTileCommand cmd;
                cmd.terrainEntity = handle;
                cmd.tileX = pendingTileX;
                cmd.tileZ = pendingTileZ;
                bool result = dispatcher.execute(cmd);
                if (!result)
                {
                    saveStatusMessage = "Tile not found or remove failed";
                    statusFrameCounter = 180;
                }
            }

            ImGui::Separator();
            ImGui::Text("World Streaming");

            {
                events::terrain::IsTerrainStreamingEnabledQuery enabledQuery;
                enabledQuery.terrainEntity = handle;
                bool streamingEnabled = dispatcher.query(enabledQuery);

                if (ImGui::Checkbox("Enable Streaming", &streamingEnabled))
                {
                    events::terrain::SetTerrainStreamingEnabledCommand cmd;
                    cmd.terrainEntity = handle;
                    cmd.enabled = streamingEnabled;
                    dispatcher.execute(cmd);
                }

                events::terrain::GetTerrainStreamingConfigQuery configQuery;
                configQuery.terrainEntity = handle;
                auto streamConfig = dispatcher.query(configQuery);

                bool configChanged = false;

                if (ImGui::SliderFloat("Load Radius", &streamConfig.loadRadius, 64.0f, 2048.0f, "%.0f"))
                    configChanged = true;

                if (ImGui::SliderFloat("Unload Radius", &streamConfig.unloadRadius, 64.0f, 2048.0f, "%.0f"))
                    configChanged = true;

                if (streamConfig.unloadRadius < streamConfig.loadRadius)
                    streamConfig.unloadRadius = streamConfig.loadRadius * 1.25f;

                if (ImGui::SliderInt("Max Loads/Frame", &streamConfig.maxLoadsPerFrame, 1, 16))
                    configChanged = true;

                if (ImGui::SliderInt("Max Unloads/Frame", &streamConfig.maxUnloadsPerFrame, 1, 16))
                    configChanged = true;

                if (configChanged)
                {
                    events::terrain::SetTerrainStreamingConfigCommand cmd;
                    cmd.terrainEntity = handle;
                    cmd.loadRadius = streamConfig.loadRadius;
                    cmd.unloadRadius = streamConfig.unloadRadius;
                    cmd.maxLoadsPerFrame = streamConfig.maxLoadsPerFrame;
                    cmd.maxUnloadsPerFrame = streamConfig.maxUnloadsPerFrame;
                    dispatcher.execute(cmd);
                }

                if (streamingEnabled && ImGui::Button("Load All Tiles"))
                {
                    events::terrain::LoadAllTilesCommand cmd;
                    cmd.terrainEntity = handle;
                    dispatcher.execute(cmd);
                }
            }

            ImGui::Separator();
            ImGui::Text("Physics");

            events::physics::HasTerrainColliderQuery hasColliderQuery;
            hasColliderQuery.terrainEntity = handle;
            bool hasCollider = dispatcher.query(hasColliderQuery);

            if (!hasCollider)
            {
                if (ImGui::Button("Add Collider"))
                {
                    events::physics::AddTerrainColliderCommand cmd;
                    cmd.terrainEntity = handle;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Creates a static HeightField collider for all tiles");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Collider Active");
                ImGui::SameLine();
                if (ImGui::Button("Remove Collider"))
                {
                    events::physics::RemoveTerrainColliderCommand cmd;
                    cmd.terrainEntity = handle;
                    dispatcher.execute(cmd);
                }

                std::vector<types::CollisionLayer> layers;
                try
                {
                    events::physics::GetCollisionLayersQuery layersQuery;
                    layers = dispatcher.query(layersQuery);
                }
                catch (...) {}

                if (layers.empty())
                    layers = types::PhysicsSettings::createDefault().layers;

                if (!layers.empty())
                {
                    std::vector<std::string> layerLabels;
                    int currentIndex = 0;
                    for (size_t i = 0; i < layers.size(); ++i)
                    {
                        layerLabels.push_back(layers[i].name + " [" + std::to_string(layers[i].index) + "]");
                        if (layers[i].index == terrain.colliderCollisionLayer)
                            currentIndex = static_cast<int>(i);
                    }

                    if (ImGui::BeginCombo("Collision Layer", layerLabels[currentIndex].c_str()))
                    {
                        for (size_t i = 0; i < layers.size(); ++i)
                        {
                            bool isSelected = (layers[i].index == terrain.colliderCollisionLayer);
                            if (ImGui::Selectable(layerLabels[i].c_str(), isSelected))
                            {
                                events::terrain::SetTerrainColliderPropertiesCommand propCmd;
                                propCmd.entity = handle;
                                propCmd.collisionLayer = layers[i].index;
                                propCmd.friction = terrain.colliderFriction;
                                propCmd.restitution = terrain.colliderRestitution;
                                dispatcher.execute(propCmd);
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                float friction = terrain.colliderFriction;
                if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f, "%.2f"))
                {
                    events::terrain::SetTerrainColliderPropertiesCommand propCmd;
                    propCmd.entity = handle;
                    propCmd.collisionLayer = terrain.colliderCollisionLayer;
                    propCmd.friction = friction;
                    propCmd.restitution = terrain.colliderRestitution;
                    dispatcher.execute(propCmd);
                }

                float restitution = terrain.colliderRestitution;
                if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f, "%.2f"))
                {
                    events::terrain::SetTerrainColliderPropertiesCommand propCmd;
                    propCmd.entity = handle;
                    propCmd.collisionLayer = terrain.colliderCollisionLayer;
                    propCmd.friction = terrain.colliderFriction;
                    propCmd.restitution = restitution;
                    dispatcher.execute(propCmd);
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

        events::terrain::SetTerrainSaveLockCommand lockCmd;
        lockCmd.locked = true;
        dispatcher.execute(lockCmd);

        // Use incremental save when the file already exists on disk
        bool useIncremental = std::filesystem::exists(path);

        events::terrain::PrepareTerrainSaveCommand prepCmd;
        prepCmd.terrainEntity = handle;
        prepCmd.incremental = useIncremental;
        dispatcher.execute(prepCmd);

        pendingSave = threading::JobSystem::instance().submit([handle, path, useIncremental]()
        {
            events::terrain::SaveTerrainCommand cmd;
            cmd.terrainEntity = handle;
            cmd.path = path;
            cmd.incremental = useIncremental;
            return events::EventDispatcher::instance().execute(cmd);
        }, threading::JobPriority::LOW);
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
