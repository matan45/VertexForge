// VK-1598 - the World Sectors window's "Repartition" and "Convert to Flat" actions.
//
// Split out of WorldSectorWindow.cpp for the same reason the Data Layers tab was: no header of its
// own, the declarations live on the window class.
//
// Both actions are destructive and both are Services-side: the Editor links neither World nor
// Serialization (premake5.lua), so everything here goes through the CQRS events and the window
// never sees a .vfsector. The dry run is an explicit button rather than a poll - it reads and
// parses every sector file in the world.

#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "events/world/HLODEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "print/Log.hpp"
#include "nfd/FileDialog.hpp"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace windows
{
    namespace
    {
        constexpr ImVec4 kErrorColor(1.0f, 0.3f, 0.3f, 1.0f);
        constexpr ImVec4 kWarnColor(0.95f, 0.75f, 0.2f, 1.0f);
        constexpr ImVec4 kOkColor(0.4f, 0.85f, 0.45f, 1.0f);

        void formatMegabytes(uint64_t bytes, char* out, size_t outSize)
        {
            std::snprintf(out, outSize, "%.1f MB",
                          static_cast<double>(bytes) / (1024.0 * 1024.0));
        }
    }

    world::SectorConfig WorldSectorWindow::currentRepartitionConfig() const
    {
        world::SectorConfig config;
        config.sectorWorldSize = repartitionSectorSize;
        config.tilesPerSector = repartitionTilesPerSector;
        config.alignedToTerrain = repartitionAlignToTerrain;
        return config;
    }

    void WorldSectorWindow::seedRepartitionFromWorld()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::world::GetSectorConfigQuery configQuery;
        configQuery.gridIndex = activeGrid;
        const auto current = dispatcher.query(configQuery);
        repartitionSectorSize = current.sectorWorldSize;
        repartitionTilesPerSector = current.tilesPerSector;
        repartitionAlignToTerrain = current.alignedToTerrain;

        terrainTileSize = dispatcher.query(events::terrain::GetActiveTerrainTileSizeQuery{});

        repartitionPreviewValid = false;
        repartitionPreview = {};
        repartitionStatus.clear();
        repartitionStatusIsError = false;
        repartitionSeeded = true;
    }

    void WorldSectorWindow::drawRepartitionSection()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::world::GetSectorConfigQuery configQuery;
        configQuery.gridIndex = activeGrid;
        const auto current = dispatcher.query(configQuery);

        ImGui::SeparatorText("Partition");
        ImGui::Text("Sector size: %.0f world units  |  %d tile(s) per sector%s",
                    current.sectorWorldSize, current.tilesPerSector,
                    current.alignedToTerrain ? "  |  terrain-aligned" : "");

        if (ImGui::Button("Repartition..."))
        {
            seedRepartitionFromWorld();
            openRepartitionModal = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Rewrite every .vfsector under a new sector size.\n"
                              "Entity UUIDs, transforms and components are preserved.\n"
                              "The outgoing set is kept as sectors.bak/.");
        }

        ImGui::SameLine();
        if (ImGui::Button("Convert to Flat..."))
        {
            openFlattenModal = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Load every sector into the scene and close the world -\n"
                              "the inverse of the creation wizard.");
        }

        if (!repartitionStatus.empty())
        {
            ImGui::TextColored(repartitionStatusIsError ? kErrorColor : kOkColor, "%s",
                               repartitionStatus.c_str());
        }
    }

    void WorldSectorWindow::drawRepartitionModals()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // OpenPopup and BeginPopupModal must share a popup-stack ID scope. drawRepartitionSection
        // runs inside a tab item, which pushes its own ID - so the flags it sets are consumed here,
        // at the window root, where the modals themselves live.
        if (openRepartitionModal)
        {
            ImGui::OpenPopup("Repartition World");
            openRepartitionModal = false;
        }
        if (openFlattenModal)
        {
            ImGui::OpenPopup("Convert World to Flat?");
            openFlattenModal = false;
        }
        if (openRebakePrompt)
        {
            ImGui::OpenPopup("HLOD Invalidated");
            openRebakePrompt = false;
        }
        if (openSaveScenePrompt)
        {
            ImGui::OpenPopup("Scene Not Saved");
            openSaveScenePrompt = false;
        }

        // ── Repartition ────────────────────────────────────────────────────────────────
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Repartition World", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (!repartitionSeeded)
                seedRepartitionFromWorld();

            // Any edit invalidates the dry run: applying a config the summary did not describe is
            // exactly the mistake the dry run exists to prevent.
            bool edited = false;

            if (terrainTileSize > 0.0f)
            {
                edited |= ImGui::Checkbox("Align to Terrain Grid", &repartitionAlignToTerrain);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Derive sector size from terrain tile size.\n"
                                      "sectorWorldSize = tilesPerSector * worldTileSize");
            }

            edited |= ImGui::InputInt("Tiles Per Sector", &repartitionTilesPerSector);
            if (repartitionTilesPerSector < 1) repartitionTilesPerSector = 1;

            if (repartitionAlignToTerrain && terrainTileSize > 0.0f)
            {
                const float derived = terrainTileSize * static_cast<float>(repartitionTilesPerSector);
                if (derived != repartitionSectorSize)
                {
                    repartitionSectorSize = derived;
                    edited = true;
                }
                ImGui::BeginDisabled();
                ImGui::InputFloat("Sector Size (auto)", &repartitionSectorSize);
                ImGui::EndDisabled();
                ImGui::TextDisabled("= %d tiles x %.0f tile size", repartitionTilesPerSector,
                                    terrainTileSize);
            }
            else
            {
                edited |= ImGui::InputFloat("Sector Size", &repartitionSectorSize, 16.0f, 64.0f);
            }

            if (edited)
            {
                repartitionPreviewValid = false;
                repartitionPreview = {};
            }

            // Same validation the creation wizard performs (WorldSectorWindow.cpp): sectorSize is
            // the divisor in worldPositionToSectorCoord, so a non-positive value collapses every
            // entity into sector 0.
            const bool sizeValid = std::isfinite(repartitionSectorSize) && repartitionSectorSize > 0.0f;
            if (!sizeValid)
                ImGui::TextColored(kErrorColor, "Sector Size must be greater than 0");
            else
                ImGui::TextDisabled("Addressable world extent: +/- %.0f units (%d sectors x %.0f)",
                                    static_cast<double>(world::kMaxSectorCoord) * repartitionSectorSize,
                                    world::kMaxSectorCoord, repartitionSectorSize);

            ImGui::Separator();

            if (!sizeValid) ImGui::BeginDisabled();
            if (ImGui::Button("Dry Run", ImVec2(110, 0)))
            {
                events::world::PreviewRepartitionQuery query;
                query.gridIndex = activeGrid;
                query.sectorConfig = currentRepartitionConfig();
                repartitionPreview = dispatcher.query(query);
                repartitionPreviewValid = repartitionPreview.valid;
            }
            if (!sizeValid) ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reads and re-buckets every .vfsector without writing anything.");

            if (!repartitionPreview.refusal.empty())
            {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(500.0f);
                ImGui::TextColored(kErrorColor, "%s", repartitionPreview.refusal.c_str());
                ImGui::PopTextWrapPos();
            }
            else if (repartitionPreviewValid)
            {
                const auto& s = repartitionPreview;
                ImGui::Spacing();
                ImGui::Text("Sectors:  %u  ->  %u", s.sourceSectorCount, s.targetSectorCount);
                ImGui::Text("Entities: %u (%u change sector)", s.entityCount, s.movedCount);

                if (s.duplicatesDropped > 0)
                {
                    ImGui::TextColored(kWarnColor,
                                       "%u duplicate entity record(s) will be collapsed.",
                                       s.duplicatesDropped);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("These sector files list the same entity more than once.\n"
                                          "The repartition writes each entity exactly once.");
                }
                if (s.carriedNonSpatial > 0)
                {
                    ImGui::TextDisabled("%u terrain/ocean/IBL/camera or always-loaded payload(s) "
                                        "carried across unchanged.", s.carriedNonSpatial);
                }
                if (s.layerCopies > 0)
                {
                    ImGui::Text("Data-layer blob copies: %u", s.layerCopies);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Blobs are opaque and cannot be split, so each one is\n"
                                          "copied to every sector its old footprint overlaps.");
                }
                if (s.layerNameCollisions > 0)
                {
                    ImGui::TextColored(kWarnColor,
                                       "%u data-layer name collision(s) - the first source wins.",
                                       s.layerNameCollisions);
                }

                char sizeStr[32];
                formatMegabytes(s.sourceBytes, sizeStr, sizeof(sizeStr));
                ImGui::Spacing();
                ImGui::TextDisabled("Peak memory: at least %s - the whole world's entity data is\n"
                                    "held at once. A stream-through variant does not exist yet.",
                                    sizeStr);
            }

            ImGui::Separator();
            ImGui::TextColored(kWarnColor, "This rewrites every sector file.");
            ImGui::TextDisabled("Save World runs first. The outgoing set is kept as sectors.bak/\n"
                                "and the world file as <world>.vfworld.bak. Every HLOD tier is\n"
                                "invalidated and has to be re-baked.");
            ImGui::Spacing();

            const bool canApply = sizeValid && repartitionPreviewValid;
            if (!canApply) ImGui::BeginDisabled();
            if (ImGui::Button("Apply", ImVec2(110, 0)))
            {
                pendingRepartitionConfig = currentRepartitionConfig();

                events::world::ApplyRepartitionCommand cmd;
                cmd.gridIndex = activeGrid;
                cmd.sectorConfig = pendingRepartitionConfig;
                const bool ok = dispatcher.execute(cmd);

                repartitionStatusIsError = !ok;
                if (ok)
                {
                    repartitionStatus = "Repartitioned. Previous sectors are in sectors.bak/.";
                    // loadWorld publishes WorldLoadedNotification, which already invalidates these -
                    // belt and braces, and it costs one bool assignment.
                    invalidateConfigCaches();

                    // Queried rather than read off the cached hlodEnabled member: that one is only
                    // filled once the HLOD tab has been opened, so a user who never touched it
                    // would silently miss the prompt.
                    const auto hlodConfig = dispatcher.query(events::world::hlod::GetHLODConfigQuery{});
                    if (hlodConfig.enabled)
                        openRebakePrompt = true;
                }
                else
                {
                    repartitionStatus = "Repartition failed - see the console.";
                }

                repartitionSeeded = false;
                ImGui::CloseCurrentPopup();
            }
            if (!canApply) ImGui::EndDisabled();
            if (!repartitionPreviewValid && sizeValid && ImGui::IsItemHovered())
                ImGui::SetTooltip("Run a dry run first.");

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0)))
            {
                repartitionSeeded = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ── Convert to flat ────────────────────────────────────────────────────────────
        if (ImGui::BeginPopupModal("Convert World to Flat?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Every sector's entities are loaded into the scene and the world is "
                               "closed. This is the inverse of the creation wizard.");
            ImGui::Spacing();
            ImGui::TextColored(kWarnColor, "The result is NOT saved anywhere until you Save Scene.");
            ImGui::TextDisabled("The sector files, the HLOD bakes and the .vfworld are moved to\n"
                                "sectors.bak/. Per-sector data layers do not survive flattening -\n"
                                "that backup is their only copy.");
            ImGui::Spacing();

            if (ImGui::Button("Convert", ImVec2(110, 0)))
            {
                const bool ok = dispatcher.execute(events::world::ConvertWorldToFlatCommand{});
                repartitionStatusIsError = !ok;
                repartitionStatus = ok ? "World flattened - Save Scene to persist it."
                                       : "Convert to flat failed - see the console.";
                if (ok)
                {
                    invalidateConfigCaches(); // clearWorld publishes no WorldLoadedNotification
                    openSaveScenePrompt = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ── Re-bake prompt ─────────────────────────────────────────────────────────────
        if (ImGui::BeginPopupModal("HLOD Invalidated", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Every HLOD tier was baked from the previous partition, so all of "
                               "them were invalidated. Distant sectors render nothing until a "
                               "re-bake finishes.");
            ImGui::Spacing();

            if (ImGui::Button("Re-bake Now", ImVec2(120, 0)))
            {
                events::world::hlod::GenerateAllHLODCommand cmd;
                cmd.missingOnly = false;
                dispatcher.execute(cmd);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Later", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ── Save-scene prompt ──────────────────────────────────────────────────────────
        if (ImGui::BeginPopupModal("Scene Not Saved", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("The flattened world lives only in the scene graph right now. "
                               "Closing or reloading the scene would discard it.");
            ImGui::Spacing();

            if (ImGui::Button("Save Scene", ImVec2(120, 0)))
            {
                std::string path = dispatcher.query(events::scene::GetCurrentScenePathQuery{});
                if (path.empty())
                {
                    // Same flow as File > Save Scene As.
                    const std::vector<std::pair<std::wstring, std::wstring>> sceneTypes = {
                        {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                    };
                    nfd::FileDialog fileDialog;
                    path = fileDialog.saveFileDialog(sceneTypes, L"vfScene");
                }

                if (!path.empty())
                {
                    events::scene::SaveSceneCommand saveCmd;
                    saveCmd.filePath = path;
                    dispatcher.execute(saveCmd);
                    repartitionStatus = "Scene saved.";
                    repartitionStatusIsError = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Not Now", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

} // namespace windows
