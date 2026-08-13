// VK-1596 - the World Sectors window's "Data Layers" tab.
//
// Split out of WorldSectorWindow.cpp, which was already ~970 lines. Same idiom as
// EditorPreferencesWindowSections.cpp / RenderConfigWindowSections.cpp: no header of its own, the
// declarations live on the window class.
//
// Semantics worth knowing before editing this file: a data layer has no "enabled" bit. It is a
// name -> bytes entry on a WorldSector, so the tab's checkbox is a PRESENCE toggle over the loaded
// sectors, driven entirely by the three pre-existing commands.

#include "WorldSectorWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "print/Log.hpp"
#include "nfd/FileDialog.hpp"
#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_MixedValue - the tri-state checkbox
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <string_view>

namespace windows
{
    const std::vector<std::pair<std::wstring, std::wstring>> WorldSectorWindow::LAYER_FILE_TYPES = {
        {L"Data Layer Blob", L"*.vflayer"},
        {L"All Files", L"*.*"}
    };

    namespace
    {
        // A layer blob is written straight into the .vfsector, and the reader hard-rejects any file
        // whose totalFileSize exceeds MAX_SECTOR_FILE_SIZE (256 MB, WorldSectorSerialization.cpp).
        // An oversized import therefore SAVES fine and then makes the whole sector permanently
        // unloadable - silent corruption discovered only on the next load. Refuse well short of the
        // limit, because the entity blob shares the same budget.
        constexpr uint64_t MAX_IMPORT_BYTES = 64ull * 1024 * 1024;
        constexpr uint64_t WARN_IMPORT_BYTES = 16ull * 1024 * 1024;

        void formatBytes(uint64_t bytes, char* out, size_t outSize)
        {
            if (bytes < 1024ull)
                snprintf(out, outSize, "%llu B", static_cast<unsigned long long>(bytes));
            else if (bytes < 1024ull * 1024)
                snprintf(out, outSize, "%.1f KB", static_cast<double>(bytes) / 1024.0);
            else
                snprintf(out, outSize, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

        [[nodiscard]] bool readWholeFile(const std::string& path, std::vector<uint8_t>& out)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
                return false;

            const std::streamoff size = file.tellg();
            if (size < 0)
                return false;
            file.seekg(0, std::ios::beg);

            out.resize(static_cast<size_t>(size));
            if (!out.empty())
                file.read(reinterpret_cast<char*>(out.data()), size);
            return file.good() || file.eof();
        }

        [[nodiscard]] bool writeWholeFile(const std::string& path, const std::vector<uint8_t>& data)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return false;
            if (!data.empty())
                file.write(reinterpret_cast<const char*>(data.data()),
                           static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        // Layer names are free-form map keys - nothing stops one containing a path separator, and
        // "Export all" builds a filename out of the name.
        [[nodiscard]] std::string sanitizeFileName(std::string_view name)
        {
            std::string safe;
            safe.reserve(name.size());
            for (const char c : name)
            {
                const bool illegal = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                                     c == '"' || c == '<' || c == '>' || c == '|' ||
                                     static_cast<unsigned char>(c) < 0x20;
                safe.push_back(illegal ? '_' : c);
            }
            return safe.empty() ? std::string{"layer"} : safe;
        }
    } // namespace

    const world::DataLayerSummary* WorldSectorWindow::findCachedLayer(const std::string& name) const
    {
        for (const auto& layer : cachedLayers.layers)
        {
            if (layer.name == name)
                return &layer;
        }
        return nullptr;
    }

    void WorldSectorWindow::refreshDataLayers()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        cachedLayers = dispatcher.query(events::world::GetDataLayersSummaryQuery{});

        // Invert once per refresh so the Sector Grid tooltip and highlight need no query at all.
        // The layer list is already name-sorted, so each per-sector list comes out sorted too.
        cachedSectorLayers.clear();
        for (const auto& layer : cachedLayers.layers)
        {
            for (const auto& coord : layer.sectors)
                cachedSectorLayers[coord].push_back(layer.name);
        }

        if (exportSectorIndex >= static_cast<int>(cachedLayers.loadedSectors.size()))
            exportSectorIndex = 0;
    }

    void WorldSectorWindow::applyLayerPresence(const std::string& layerName, bool present)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        layerError.clear();

        uint32_t refused = 0;
        for (const auto& coord : cachedLayers.loadedSectors)
        {
            if (present)
            {
                events::world::SetSectorDataLayerCommand cmd;
                cmd.coord = coord;
                cmd.layerName = layerName;

                // Re-checking restores whatever this session's un-check stashed for that sector;
                // a layer that was never detached is (re)created empty rather than copying bytes
                // from a sibling sector, which would be a surprising implicit data copy.
                if (const auto stash = detachedLayers.find(layerName); stash != detachedLayers.end())
                {
                    if (const auto blob = stash->second.find(coord); blob != stash->second.end())
                        cmd.data = blob->second;
                }

                if (!dispatcher.execute(cmd))
                    ++refused;
            }
            else
            {
                // Stash before removing. Export is the durable backup the ticket intends; this is
                // the cheap net for a mis-click, and it is why un-check is recoverable in-session.
                events::world::GetSectorDataLayerQuery query;
                query.coord = coord;
                query.layerName = layerName;
                if (auto blob = dispatcher.query(query); blob.has_value())
                    detachedLayers[layerName][coord] = std::move(*blob);

                events::world::RemoveSectorDataLayerCommand cmd;
                cmd.coord = coord;
                cmd.layerName = layerName;
                // A false here usually just means "this sector never carried it" (the normal mixed
                // case), so it is deliberately not counted as a refusal.
                dispatcher.execute(cmd);
            }
        }

        if (present)
            detachedLayers.erase(layerName); // consumed by the restore above

        if (refused > 0)
        {
            layerError = std::format(
                "{} sector(s) refused the change - only fully loaded sectors can be edited.",
                refused);
        }

        refreshDataLayers();
    }

    void WorldSectorWindow::drawDeleteLayerModal()
    {
        if (!ImGui::BeginPopupModal("Delete Data Layer?", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
            return;

        char sizeStr[32];
        formatBytes(pendingDeleteBytes, sizeStr, sizeof(sizeStr));

        ImGui::Text("Remove \"%s\" from %u sector(s)?", pendingDeleteLayer.c_str(),
                    pendingDeleteSectorCount);
        ImGui::TextDisabled("%s of stored bytes will be destroyed.", sizeStr);
        ImGui::Spacing();
        ImGui::TextDisabled("Export first if you want a backup. The removal becomes permanent\n"
                            "on the next Save World.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(80, 0)))
        {
            const std::string layerName = pendingDeleteLayer;
            applyLayerPresence(layerName, false);
            // A deliberate Delete is not a mis-click - drop the stash so the row cannot be
            // resurrected by re-creating a layer of the same name.
            detachedLayers.erase(layerName);
            if (selectedLayer == layerName)
                selectedLayer.clear();
            pendingDeleteLayer.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            pendingDeleteLayer.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void WorldSectorWindow::drawDataLayers()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        const bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

        if (isPlayMode)
        {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                               "Play mode - data layers are read-only.");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "Layers written in play mode are never marked dirty, so Save World\n"
                    "will not persist them - and leaving play mode clears every sector,\n"
                    "which destroys them outright. Edit layers in edit mode.");
            }
            ImGui::Spacing();
        }

        const size_t scopeCount = cachedLayers.loadedSectors.size();
        ImGui::Text("Layers across %zu loaded sector(s)", scopeCount);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Unloaded sectors are not listed: reading their layers would need a\n"
                "per-file index scan. Load a sector from the Sector Grid tab to see\n"
                "and edit its layers.");
        }

        if (scopeCount == 0)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No sectors loaded. Click a sector in the Sector Grid tab to load "
                                "one.");
            return;
        }

        ImGui::Spacing();

        // Deferred so that nothing mutates cachedLayers while the loop below iterates it:
        // applyLayerPresence re-runs the summary query, which would invalidate `layer`.
        std::string pendingToggleLayer;
        bool pendingToggleValue = false;
        bool openDeleteModal = false;

        if (ImGui::BeginTable("DataLayersTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0.0f, 180.0f)))
        {
            // The checkbox gets its own column rather than sharing the name cell: a Selectable
            // drawn after it on the same line would cover it and swallow the clicks.
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("Layer");
            ImGui::TableSetupColumn("Sectors", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& layer : cachedLayers.layers)
            {
                ImGui::TableNextRow();
                ImGui::PushID(layer.name.c_str());

                ImGui::TableNextColumn();
                const bool onAll = layer.sectorCount >= scopeCount;
                const bool mixed = layer.sectorCount > 0 && !onAll;

                // Seeded from onAll, NOT from "carried anywhere". With MixedValue the box renders
                // as a dash either way, so seeding it false is what makes a click on a partially
                // applied layer ADD it everywhere rather than wipe it - the conventional tri-state
                // affordance, and the non-destructive direction.
                bool present = onAll;

                ImGui::BeginDisabled(isPlayMode);
                if (mixed)
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox("##present", &present))
                {
                    pendingToggleLayer = layer.name;
                    pendingToggleValue = present;
                }
                if (mixed)
                    ImGui::PopItemFlag();
                ImGui::EndDisabled();

                ImGui::TableNextColumn();
                if (ImGui::Selectable(layer.name.c_str(), selectedLayer == layer.name))
                    selectedLayer = (selectedLayer == layer.name) ? std::string{} : layer.name;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Select to highlight the sectors carrying this layer\n"
                                      "in the Sector Grid tab.");

                ImGui::TableNextColumn();
                ImGui::Text("%u / %zu", layer.sectorCount, scopeCount);

                ImGui::TableNextColumn();
                char sizeStr[32];
                formatBytes(layer.totalBytes, sizeStr, sizeof(sizeStr));
                ImGui::TextUnformatted(sizeStr);

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(isPlayMode);
                if (ImGui::SmallButton("Delete"))
                {
                    // Latch what the dialog is about to promise. The summary repolls every 0.25s
                    // and streaming can change the loaded set while the modal is open, so reading
                    // these live would let the confirmation destroy more than it named.
                    pendingDeleteLayer = layer.name;
                    pendingDeleteSectorCount = layer.sectorCount;
                    pendingDeleteBytes = layer.totalBytes;
                    openDeleteModal = true;
                }
                ImGui::EndDisabled();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (!pendingToggleLayer.empty())
            applyLayerPresence(pendingToggleLayer, pendingToggleValue);

        // OpenPopup and BeginPopupModal must share a popup-stack ID scope, so both happen here at
        // the tab's root scope - never inside the table's PushID(layer.name).
        if (openDeleteModal)
            ImGui::OpenPopup("Delete Data Layer?");
        drawDeleteLayerModal();

        if (cachedLayers.layers.empty())
            ImGui::TextDisabled("No data layers on the loaded sectors yet.");

        // The merge semantics make removal asymmetric, and hiding that would be a trap: the file
        // merges UNDER memory on load, so a layer removed in memory is restored from disk by the
        // next sector reload until the world is saved.
        bool anyPartial = false;
        for (const auto& layer : cachedLayers.layers)
            anyPartial |= (layer.sectorCount > 0 && layer.sectorCount < scopeCount);
        if (anyPartial)
        {
            ImGui::TextDisabled("Removing a layer only becomes permanent on Save World. Until "
                                "then,\nreloading a sector restores it from disk.");
        }

        ImGui::Separator();

        // ── Create ────────────────────────────────────────────────────────────────────
        ImGui::BeginDisabled(isPlayMode);
        ImGui::SetNextItemWidth(200.0f);
        const bool submitted = ImGui::InputTextWithHint("##newLayer", "New layer name",
                                                        newLayerName, sizeof(newLayerName),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);

        const bool nameEmpty = newLayerName[0] == '\0';
        // SetSectorDataLayerCommand assigns unconditionally, so creating over an existing name
        // would silently zero its blob on every loaded sector. Block it rather than confirm it -
        // the name is free to choose.
        const bool duplicate = !nameEmpty && findCachedLayer(newLayerName) != nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(nameEmpty || duplicate);
        if ((ImGui::Button("Create") || submitted) && !nameEmpty && !duplicate)
        {
            applyLayerPresence(newLayerName, true);
            newLayerName[0] = '\0';
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("on %zu loaded sector(s)", scopeCount);
        ImGui::EndDisabled();

        constexpr ImVec4 errorColor(1.0f, 0.3f, 0.3f, 1.0f);
        if (duplicate)
            ImGui::TextColored(errorColor, "A layer named \"%s\" already exists", newLayerName);
        if (!layerError.empty())
            ImGui::TextColored(errorColor, "%s", layerError.c_str());

        // ── Selected layer: per-sector import/export ──────────────────────────────────
        if (selectedLayer.empty())
            return;

        ImGui::Separator();
        ImGui::Text("Layer \"%s\"", selectedLayer.c_str());

        // A blob belongs to ONE sector, so import/export are per-sector by nature - hence the
        // picker rather than a scope-wide button.
        std::string preview = "(none)";
        if (exportSectorIndex < static_cast<int>(cachedLayers.loadedSectors.size()))
        {
            const auto& coord = cachedLayers.loadedSectors[exportSectorIndex];
            preview = std::format("({}, {})", coord.x, coord.z);
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::BeginCombo("Sector", preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(cachedLayers.loadedSectors.size()); ++i)
            {
                const auto& coord = cachedLayers.loadedSectors[i];
                const std::string label = std::format("({}, {})", coord.x, coord.z);
                if (ImGui::Selectable(label.c_str(), exportSectorIndex == i))
                    exportSectorIndex = i;
            }
            ImGui::EndCombo();
        }

        if (exportSectorIndex >= static_cast<int>(cachedLayers.loadedSectors.size()))
            return;

        const world::SectorCoord target = cachedLayers.loadedSectors[exportSectorIndex];

        // Presence comes from the cached summary, NOT from a live GetSectorDataLayerQuery: that
        // query returns the blob by value, so polling it per frame just to grey out a button would
        // copy megabytes every frame. The bytes are fetched once, inside the click handler.
        const world::DataLayerSummary* selectedSummary = findCachedLayer(selectedLayer);
        const bool targetHasLayer =
            selectedSummary != nullptr &&
            std::find(selectedSummary->sectors.begin(), selectedSummary->sectors.end(), target) !=
                selectedSummary->sectors.end();

        ImGui::BeginDisabled(!targetHasLayer);
        if (ImGui::Button("Export..."))
        {
            events::world::GetSectorDataLayerQuery blobQuery;
            blobQuery.coord = target;
            blobQuery.layerName = selectedLayer;
            const auto targetBlob = dispatcher.query(blobQuery);

            nfd::FileDialog fileDialog;
            const std::string path = fileDialog.saveFileDialog(LAYER_FILE_TYPES, L"vflayer");
            if (!path.empty() && targetBlob.has_value())
            {
                if (writeWholeFile(path, *targetBlob))
                {
                    layerError.clear();
                    vfLogInfo("Exported data layer '{}' of sector ({},{}) to {}",
                              selectedLayer, target.x, target.z, path);
                }
                else
                {
                    layerError = std::format("Failed to write {}", path);
                }
            }
        }
        ImGui::EndDisabled();
        if (!targetHasLayer && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Sector (%d, %d) does not carry this layer.", target.x, target.z);

        ImGui::SameLine();
        ImGui::BeginDisabled(isPlayMode);
        if (ImGui::Button("Import..."))
        {
            nfd::FileDialog fileDialog;
            const std::string path = fileDialog.openFileDialog(LAYER_FILE_TYPES);
            if (!path.empty())
            {
                std::vector<uint8_t> bytes;
                if (!readWholeFile(path, bytes))
                {
                    layerError = std::format("Failed to read {}", path);
                }
                else if (bytes.size() > MAX_IMPORT_BYTES)
                {
                    // Refused, not truncated: writing it would produce a .vfsector the loader
                    // rejects outright, i.e. the sector would be lost on its next load.
                    layerError = std::format(
                        "Blob is {:.1f} MB - the limit is {} MB, above which the sector file "
                        "would exceed what the loader accepts.",
                        static_cast<double>(bytes.size()) / (1024.0 * 1024.0),
                        MAX_IMPORT_BYTES / (1024 * 1024));
                }
                else
                {
                    if (bytes.size() > WARN_IMPORT_BYTES)
                    {
                        vfLogWarning("Data layer '{}' import is {:.1f} MB - large layers inflate "
                                     "every save of sector ({},{})",
                                     selectedLayer,
                                     static_cast<double>(bytes.size()) / (1024.0 * 1024.0),
                                     target.x, target.z);
                    }

                    events::world::SetSectorDataLayerCommand cmd;
                    cmd.coord = target;
                    cmd.layerName = selectedLayer;
                    cmd.data = std::move(bytes);
                    if (dispatcher.execute(cmd))
                    {
                        layerError.clear();
                        refreshDataLayers();
                    }
                    else
                    {
                        layerError = "Import refused - the sector is no longer loaded.";
                    }
                }
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Replace this sector's blob with the contents of a file.\n"
                              "Raw bytes, no header - the export format round-trips.");

        ImGui::SameLine();
        if (ImGui::Button("Export all sectors..."))
        {
            nfd::FileDialog fileDialog;
            const std::string folder = fileDialog.selectFolderDialog();
            if (!folder.empty() && selectedSummary != nullptr)
            {
                uint32_t written = 0;
                uint32_t failed = 0;
                const std::string safeName = sanitizeFileName(selectedLayer);

                for (const auto& coord : selectedSummary->sectors)
                {
                    events::world::GetSectorDataLayerQuery q;
                    q.coord = coord;
                    q.layerName = selectedLayer;
                    const auto blob = dispatcher.query(q);
                    if (!blob.has_value())
                        continue;

                    const std::filesystem::path outPath =
                        std::filesystem::path(folder) /
                        std::format("{}_{}_{}.vflayer", safeName, coord.x, coord.z);
                    if (writeWholeFile(outPath.string(), *blob))
                        ++written;
                    else
                        ++failed;
                }

                layerError = failed > 0
                                 ? std::format("Exported {} file(s), {} failed", written, failed)
                                 : std::string{};
                vfLogInfo("Exported data layer '{}' for {} sector(s) to {}", selectedLayer, written,
                          folder);
            }
        }
    }

} // namespace windows
