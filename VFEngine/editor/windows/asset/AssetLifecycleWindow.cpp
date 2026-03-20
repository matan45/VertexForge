#include "AssetLifecycleWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/lifecycle/AssetLifecycleEvents.hpp"
#include "asset/AssetRef.hpp"
#include "imgui.h"
#include <algorithm>

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
            typeCounts[static_cast<int>(resource::AssetType::MaterialInstance)] +
            typeCounts[static_cast<int>(resource::AssetType::SVT)];

        ImGui::TextDisabled("Tex:%u Mesh:%u Audio:%u Anim:%u Mat:%u SVT:%u Other:%u",
                            typeCounts[static_cast<int>(resource::AssetType::Texture)],
                            typeCounts[static_cast<int>(resource::AssetType::Mesh)],
                            typeCounts[static_cast<int>(resource::AssetType::Audio)],
                            typeCounts[static_cast<int>(resource::AssetType::Animation)],
                            typeCounts[static_cast<int>(resource::AssetType::Material)] +
                            typeCounts[static_cast<int>(resource::AssetType::MaterialInstance)],
                            typeCounts[static_cast<int>(resource::AssetType::SVT)],
                            totalAssets - knownCount);
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
                                     "BehaviorTree", "World", "SVT"};
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
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

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
