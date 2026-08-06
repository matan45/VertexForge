#include "TerrainDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/world/WorldSectorEvents.hpp" // VK-1613: IsWorldModeQuery gates the streaming block
#include "events/physics/PhysicsEvents.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "types/PhysicsTypes.hpp"
#include <imgui.h>
#include <filesystem>
#include <chrono>
#include <iterator>

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

            drawInfo(terrain);
            drawSaveLoad(handle, terrain);
            drawGridExpansion(handle);
            drawStreaming(handle);
            drawSurfaceMask(handle);
            drawPhysics(handle, terrain);

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        return true;
    }

    void TerrainDrawer::drawInfo(const services::TerrainData& terrain)
    {
        // VK-1613: "Ultra (257x257)" was a 4th label for an enumerator that does not exist —
        // terrain::TileResolution stops at High/129 (TerrainTypes.hpp), and TerrainCreationWindow
        // correctly offers three. Bound by the array itself so the two can never drift again.
        const char* resolutionNames[] = { "Low (33x33)", "Medium (65x65)", "High (129x129)" };
        int resIndex = static_cast<int>(terrain.resolution);
        if (resIndex >= 0 && resIndex < static_cast<int>(std::size(resolutionNames)))
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

        // VK-1613: "Active" and "Dirty" used to be printed here, but TerrainComponent::isActive is
        // only ever written `true` and isDirty only ever `false` (TerrainCreationOps /
        // TerrainPersistenceOps), so both were constants dressed up as state. Removed rather than
        // wired: neither has a meaning anything acts on. "Visible Tiles" IS real now — the main
        // camera's visibility pass fills it (TerrainVisibilityOps::getRawVisibleTiles).
        ImGui::Text("Active Tiles: %u", terrain.activeTileCount);
        ImGui::Text("Visible Tiles: %u", terrain.visibleTileCount);

        if (!terrain.heightmapPath.empty())
        {
            ImGui::Separator();
            ImGui::Text("Heightmap:");
            ImGui::TextWrapped("%s", terrain.heightmapPath.c_str());
        }

        if (!terrain.heightmapRegions.empty())
        {
            ImGui::Separator();
            if (ImGui::TreeNode("Heightmap Regions"))
            {
                for (size_t i = 0; i < terrain.heightmapRegions.size(); ++i)
                {
                    const auto& region = terrain.heightmapRegions[i];
                    ImGui::Text("Region %zu: [%d,%d] - [%d,%d]",
                        i + 1, region.tileMinX, region.tileMinZ, region.tileMaxX, region.tileMaxZ);
                    ImGui::TextWrapped("  %s", region.filePath.c_str());
                }
                ImGui::TreePop();
            }
        }
    }

    void TerrainDrawer::drawSaveLoad(services::EntityHandle handle, const services::TerrainData& terrain)
    {
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
    }

    void TerrainDrawer::drawGridExpansion(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

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
    }

    void TerrainDrawer::drawStreaming(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Separator();
        ImGui::Text("World Streaming");

        // VK-1613: in World mode the sector streamer owns terrain tile streaming, and BOTH commands
        // below early-return on it (TerrainService's SetTerrainStreamingEnabled /
        // SetTerrainStreamingConfig handlers). These controls were therefore silently inert there —
        // they just snapped back with no explanation. Say so instead of pretending.
        const bool worldMode = dispatcher.query(events::world::IsWorldModeQuery{});
        ImGui::BeginDisabled(worldMode);

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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Streams terrain tiles in/out based on camera distance.\n\n"
                              "Requirements:\n"
                              "  - Terrain must be saved to a .vfTerrain file first\n"
                              "  - Streaming loads tiles from disk on demand\n"
                              "  - Distant tiles are unloaded to save memory\n\n"
                              "Save the terrain before enabling streaming.");
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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Loads all saved tiles and disables streaming for this session.\n"
                              "Re-enable streaming via the checkbox above.");
        }

        ImGui::EndDisabled();

        if (worldMode)
        {
            ImGui::TextDisabled("Managed by the world sector streamer");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("This terrain is part of a world. Tile streaming follows sector\n"
                                  "residency, so these per-terrain settings are ignored — tune them\n"
                                  "in the World Sector window instead.");
            }
        }
    }

    void TerrainDrawer::drawPhysics(services::EntityHandle handle, const services::TerrainData& terrain)
    {
        auto& dispatcher = events::EventDispatcher::instance();

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

            ImGui::Spacing();
            ImGui::Text("Collider Streaming");

            events::physics::GetPhysicsColliderStreamConfigQuery streamQuery;
            auto streamCfg = dispatcher.query(streamQuery);

            bool streamConfigChanged = false;

            float budgetMB = streamCfg.memoryBudgetMB;
            if (ImGui::SliderFloat("Memory Budget (MB)", &budgetMB, 8.0f, 512.0f, "%.0f"))
            {
                streamCfg.memoryBudgetMB = budgetMB;
                streamConfigChanged = true;
            }

            int maxCreations = streamCfg.maxCreationsPerFrame;
            if (ImGui::SliderInt("Max Creations/Frame", &maxCreations, 1, 16))
            {
                streamCfg.maxCreationsPerFrame = maxCreations;
                streamConfigChanged = true;
            }

            float lod0 = streamCfg.lodDistance0;
            if (ImGui::SliderFloat("Full LOD Distance", &lod0, 16.0f, 512.0f, "%.0f"))
            {
                streamCfg.lodDistance0 = lod0;
                streamConfigChanged = true;
            }

            float lod1 = streamCfg.lodDistance1;
            if (ImGui::SliderFloat("Half LOD Distance", &lod1, 64.0f, 1024.0f, "%.0f"))
            {
                streamCfg.lodDistance1 = lod1;
                streamConfigChanged = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Beyond this distance tiles use half-resolution colliders");

            float lod2 = streamCfg.lodDistance2;
            if (ImGui::SliderFloat("Quarter LOD Distance", &lod2, 128.0f, 2048.0f, "%.0f"))
            {
                streamCfg.lodDistance2 = lod2;
                streamConfigChanged = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Beyond this distance tiles use quarter-resolution colliders");

            // Enforce ordering: lod0 < lod1 < lod2
            if (streamCfg.lodDistance1 <= streamCfg.lodDistance0)
                streamCfg.lodDistance1 = streamCfg.lodDistance0 + 10.0f;
            if (streamCfg.lodDistance2 <= streamCfg.lodDistance1)
                streamCfg.lodDistance2 = streamCfg.lodDistance1 + 10.0f;

            if (streamConfigChanged)
            {
                events::physics::SetPhysicsColliderStreamConfigCommand cmd;
                cmd.terrainEntity = handle;
                cmd.memoryBudgetMB = streamCfg.memoryBudgetMB;
                cmd.maxCreationsPerFrame = streamCfg.maxCreationsPerFrame;
                cmd.lodDistance0 = streamCfg.lodDistance0;
                cmd.lodDistance1 = streamCfg.lodDistance1;
                cmd.lodDistance2 = streamCfg.lodDistance2;
                dispatcher.execute(cmd);
            }
        }
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

    // VK-1614 world-anchored wetness/snow mask (R = wetness, G = snow).
    //
    // Lives on the terrain rather than on the terrain material: two terrains sharing a
    // .vfTerrainMat must not share one puddle map. The world rect is snapshotted from the terrain's
    // CURRENT bounds when the mask is created and never recomputed — see components::TerrainComponent
    // for why deriving it live would slide every painted puddle on grid expansion.
    void TerrainDrawer::drawSurfaceMask(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        ImGui::Spacing();
        if (!ImGui::CollapsingHeader("Surface Mask"))
            return;

        // Scoped to the terrain this panel is drawn for. The service holds one mask; asking without
        // the entity would report another terrain's mask here and offer to clear it.
        events::terrain::HasSurfaceMaskQuery hasMaskQuery;
        hasMaskQuery.terrainEntity = handle;
        const bool hasMask = dispatcher.query(hasMaskQuery);

        static constexpr const char* resLabels[] = {"512", "1024", "2048", "4096"};
        static constexpr uint32_t resValues[] = {512u, 1024u, 2048u, 4096u};

        if (!hasMask)
        {
            ImGui::TextDisabled("No mask. Create one to paint local wetness and snow.");
            ImGui::SetNextItemWidth(100.0f);
            ImGui::Combo("Resolution", &surfaceMaskResIndex, resLabels,
                         static_cast<int>(std::size(resLabels)));

            if (ImGui::Button("Create Mask"))
            {
                events::terrain::CreateSurfaceMaskCommand cmd;
                cmd.terrainEntity = handle;
                cmd.resolution = resValues[surfaceMaskResIndex];
                dispatcher.execute(cmd);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load..."))
            {
                // .vfImage only — the project rule for terrain image assets.
                std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                    {L"VF Image (*.vfImage)", L"*.vfImage"}
                };
                std::string path = fileDialog.openFileDialog(fileTypes);
                if (!path.empty())
                {
                    events::terrain::LoadSurfaceMaskCommand cmd;
                    cmd.terrainEntity = handle;
                    cmd.path = path;
                    dispatcher.execute(cmd);
                }
            }
            return;
        }

        ImGui::TextDisabled("R = wetness, G = snow. Paint it with the Paint tool.");

        if (ImGui::Button("Save As..."))
        {
            std::vector<std::pair<std::wstring, std::wstring>> fileTypes = {
                {L"VF Image (*.vfImage)", L"*.vfImage"}
            };
            std::string path = fileDialog.saveFileDialog(fileTypes, L"vfImage");
            if (!path.empty())
            {
                events::terrain::SaveSurfaceMaskCommand cmd;
                cmd.terrainEntity = handle;
                cmd.path = path;
                dispatcher.execute(cmd);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Mask"))
        {
            // Discards unsaved paint; the mask is only persisted on an explicit Save As.
            events::terrain::ClearSurfaceMaskCommand cmd;
            cmd.terrainEntity = handle;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Removes the mask from this terrain.\n"
                              "Unsaved painting is lost — Save As first to keep it.");
        }
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
        if (isSaving && pendingSave.valid())
        {
            if (pendingSave.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                bool success = pendingSave.get();
                isSaving = false;

                auto& dispatcher = events::EventDispatcher::instance();

                // VK-1648. The save ran on a JobSystem worker and parked its component writes and
                // its TerrainSavedNotification rather than applying them there — TerrainComponent
                // is main-thread state. This is the main thread, and the worker is provably done
                // (its future was just consumed), so apply them now, before the save lock drops
                // and anything else can start writing to the same component.
                dispatcher.execute(events::terrain::FlushTerrainSaveResultsCommand{});

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

}
