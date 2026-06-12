#include "AssetLifecycleWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/lifecycle/AssetLifecycleEvents.hpp"
#include "asset/AssetRef.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace windows
{
    void AssetLifecycleWindow::draw()
    {
        if (!visible) return;

        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshData();
            refreshTimer = 0.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Asset Lifecycle", &visible))
        {
            drawSummary();
            ImGui::Separator();

            if (ImGui::BeginTabBar("AssetLifecycleTabs"))
            {
                if (ImGui::BeginTabItem("Loaded Assets"))
                {
                    drawAssetTable();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Pending Release"))
                {
                    drawPendingQueue();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void AssetLifecycleWindow::refreshData()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        cachedAssets = dispatcher.query(events::lifecycle::QueryAssetStatsQuery{});
        cachedPending = dispatcher.query(events::lifecycle::QueryPendingReleasesQuery{});

        auto budgetStatus = dispatcher.query(events::lifecycle::QueryMemoryBudgetQuery{});
        budgetBytes = budgetStatus.totalBudgetBytes;
        trackedBytes = budgetStatus.trackedBytes;
        overBudget = budgetStatus.overBudget;
        if (!budgetEditInitialized)
        {
            budgetEditMb = static_cast<int>(budgetBytes / (1024 * 1024));
            budgetEditInitialized = true;
        }
    }

    void AssetLifecycleWindow::drawSummary()
    {
        uint32_t totalAssets = static_cast<uint32_t>(cachedAssets.size());
        uint32_t pendingCount = static_cast<uint32_t>(cachedPending.size());
        size_t totalMemory = 0;

        uint32_t typeCounts[static_cast<int>(resource::AssetType::COUNT)] = {};
        for (const auto& asset : cachedAssets)
        {
            totalMemory += asset.estimatedMemoryBytes;
            int idx = static_cast<int>(asset.type);
            if (idx < static_cast<int>(resource::AssetType::COUNT))
            {
                typeCounts[idx]++;
            }
        }

        ImGui::Text("Total: %u assets | Pending release: %u | Memory: %.1f MB",
                     totalAssets, pendingCount,
                     static_cast<float>(totalMemory) / (1024.0f * 1024.0f));

        uint32_t knownCount =
            typeCounts[static_cast<int>(resource::AssetType::Texture)] +
            typeCounts[static_cast<int>(resource::AssetType::Mesh)] +
            typeCounts[static_cast<int>(resource::AssetType::Audio)] +
            typeCounts[static_cast<int>(resource::AssetType::Animation)] +
            typeCounts[static_cast<int>(resource::AssetType::Material)] +
            typeCounts[static_cast<int>(resource::AssetType::MaterialInstance)];

        ImGui::TextDisabled("Tex:%u Mesh:%u Audio:%u Anim:%u Mat:%u Other:%u",
                            typeCounts[static_cast<int>(resource::AssetType::Texture)],
                            typeCounts[static_cast<int>(resource::AssetType::Mesh)],
                            typeCounts[static_cast<int>(resource::AssetType::Audio)],
                            typeCounts[static_cast<int>(resource::AssetType::Animation)],
                            typeCounts[static_cast<int>(resource::AssetType::Material)] +
                            typeCounts[static_cast<int>(resource::AssetType::MaterialInstance)],
                            totalAssets - knownCount);

        // Memory budget gauge + control. Over budget, the lifecycle manager
        // releases unreferenced assets immediately (no grace period) until
        // the tracked total is back under; referenced assets are never evicted.
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("Budget (MB)", &budgetEditMb, 0, 0))
        {
            budgetEditMb = std::max(budgetEditMb, 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply"))
        {
            events::lifecycle::SetMemoryBudgetCommand cmd;
            cmd.totalBudgetBytes = static_cast<size_t>(budgetEditMb) * 1024 * 1024;
            events::EventDispatcher::instance().execute(cmd);
            refreshTimer = REFRESH_INTERVAL;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(0 disables)");

        if (budgetBytes > 0)
        {
            float usedMb = static_cast<float>(trackedBytes) / (1024.0f * 1024.0f);
            float budgetMb = static_cast<float>(budgetBytes) / (1024.0f * 1024.0f);
            float fraction = (budgetMb > 0.0f) ? usedMb / budgetMb : 0.0f;

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.1f / %.0f MB", usedMb, budgetMb);
            if (overBudget)
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
            ImGui::ProgressBar(std::min(fraction, 1.0f), ImVec2(-1.0f, 0.0f), overlay);
            if (overBudget)
            {
                ImGui::PopStyleColor();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                    "Over budget — unreferenced assets release without grace");
            }
        }
    }

    void AssetLifecycleWindow::drawAssetTable()
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Filter:");
        ImGui::SameLine();
        const char* filterNames[] = {"All", "Texture", "Mesh", "Audio", "Animation",
                                     "Animator", "Material", "MaterialInstance",
                                     "PhysicsShape", "VFX", "Script", "HDR", "Font", "Skeleton",
                                     "Navmesh", "InputMapping", "Terrain", "TerrainMaterial",
                                     "BehaviorTree", "World"};
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("##TypeFilter", &filterType, filterNames, IM_ARRAYSIZE(filterNames));

        int effectiveFilter = filterType - 1;

        if (ImGui::BeginTable("AssetsTable", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Action",
                                    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 80);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Sort the cached snapshot in place (Memory descending = top
            // consumers view). Re-applied every frame because refreshData()
            // replaces the vector in query order every 0.5s.
            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
            {
                if (sortSpecs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
                    std::stable_sort(cachedAssets.begin(), cachedAssets.end(),
                        [&spec, ascending](const resource::AssetEntry& a, const resource::AssetEntry& b)
                        {
                            bool less = false;
                            switch (spec.ColumnIndex)
                            {
                            case 1: less = static_cast<int>(a.type) < static_cast<int>(b.type); break;
                            case 2: less = a.refCount < b.refCount; break;
                            case 3: less = a.estimatedMemoryBytes < b.estimatedMemoryBytes; break;
                            default: less = a.guid.toString() < b.guid.toString(); break;
                            }
                            return ascending ? less : !less;
                        });
                    sortSpecs->SpecsDirty = false;
                }
            }

            for (const auto& asset : cachedAssets)
            {
                if (effectiveFilter >= 0 && static_cast<int>(asset.type) != effectiveFilter)
                {
                    continue;
                }

                std::string assetPath = asset::AssetRef::fromGUID(asset.guid).resolve();
                if (assetPath.empty()) assetPath = asset.guid.toString();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                auto lastSlash = assetPath.find_last_of("/\\");
                const char* displayPath = (lastSlash != std::string::npos)
                                              ? assetPath.c_str() + lastSlash + 1
                                              : assetPath.c_str();
                ImGui::TextUnformatted(displayPath);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", assetPath.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource::assetTypeName(asset.type));

                ImGui::TableNextColumn();
                if (asset.state == resource::AssetState::PendingRelease)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%u", asset.refCount);
                }
                else
                {
                    ImGui::Text("%u", asset.refCount);
                }

                ImGui::TableNextColumn();
                if (asset.estimatedMemoryBytes > 0)
                {
                    float mb = static_cast<float>(asset.estimatedMemoryBytes) / (1024.0f * 1024.0f);
                    if (mb >= 1.0f)
                    {
                        ImGui::Text("%.1f MB", mb);
                    }
                    else
                    {
                        float kb = static_cast<float>(asset.estimatedMemoryBytes) / 1024.0f;
                        ImGui::Text("%.0f KB", kb);
                    }
                }
                else
                {
                    ImGui::TextDisabled("--");
                }

                ImGui::TableNextColumn();
                ImGui::PushID(assetPath.c_str());
                if (ImGui::SmallButton("Force Free"))
                {
                    events::lifecycle::ForceReleaseAssetCommand cmd;
                    cmd.path = assetPath;
                    events::EventDispatcher::instance().execute(cmd);
                    refreshTimer = REFRESH_INTERVAL; // trigger refresh next frame
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    void AssetLifecycleWindow::drawPendingQueue()
    {
        if (cachedPending.empty())
        {
            ImGui::TextDisabled("No assets pending release.");
            return;
        }

        if (ImGui::BeginTable("PendingTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                              ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("Grace (s)", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& asset : cachedPending)
            {
                std::string pendingPath = asset::AssetRef::fromGUID(asset.guid).resolve();
                if (pendingPath.empty()) pendingPath = asset.guid.toString();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                auto lastSlash = pendingPath.find_last_of("/\\");
                const char* displayPath = (lastSlash != std::string::npos)
                                              ? pendingPath.c_str() + lastSlash + 1
                                              : pendingPath.c_str();
                ImGui::TextUnformatted(displayPath);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", pendingPath.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource::assetTypeName(asset.type));

                ImGui::TableNextColumn();
                float remaining = asset.graceTimeRemaining;
                ImVec4 color = (remaining < 1.0f)
                                   ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                                   : ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
                ImGui::TextColored(color, "%.1f", remaining);

                ImGui::TableNextColumn();
                ImGui::PushID(pendingPath.c_str());
                if (ImGui::SmallButton("Force Free"))
                {
                    events::lifecycle::ForceReleaseAssetCommand cmd;
                    cmd.path = pendingPath;
                    events::EventDispatcher::instance().execute(cmd);
                    refreshTimer = REFRESH_INTERVAL;
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
}
