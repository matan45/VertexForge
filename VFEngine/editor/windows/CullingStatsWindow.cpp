#include "CullingStatsWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include <imgui.h>
#include <string>

namespace windows
{
    void CullingStatsWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Culling Stats", &visible))
        {
            events::render::GetCullingStatsQuery query;
            auto stats = events::EventDispatcher::instance().query(query);

            ImGui::Text("Active Camera: %u", stats.activeCameraId);
            ImGui::Separator();

            if (ImGui::CollapsingHeader("GPU-Driven Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                const auto& gpu = stats.gpuDriven;

                // Status indicators
                ImGui::Text("Status:");
                ImGui::SameLine();
                ImGui::TextColored(gpu.enabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                   gpu.enabled ? "ENABLED" : "DISABLED");

                if (gpu.enabled)
                {
                    ImGui::Text("Object-Level Features:");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.frustumCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "Frustum");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.occlusionCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| Hi-Z Occlusion");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.lodSelectionEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| LOD");

                    ImGui::Text("Meshlet-Level Features:");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.meshletFrustumCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "Frustum");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.meshletBackfaceCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| Backface");

                    ImGui::Separator();

                    // Object counts
                    ImGui::Text("Objects: %u total", gpu.totalObjects);
                    if (gpu.totalObjects > 0)
                    {
                        ImGui::Text("  Culled by Frustum:   %u", gpu.culledByFrustum);
                        ImGui::Text("  Culled by Occlusion: %u", gpu.culledByOcclusion);
                        ImGui::Text("  Visible:             %u", gpu.visibleObjects);

                        // Culling efficiency
                        uint32_t totalCulled = gpu.culledByFrustum + gpu.culledByOcclusion;
                        float cullRate = static_cast<float>(totalCulled) / static_cast<float>(gpu.totalObjects);
                        ImGui::Text("Cull Rate:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(cullRate, ImVec2(150, 0),
                                           (std::to_string(static_cast<int>(cullRate * 100)) + "%").c_str());
                    }

                    ImGui::Separator();

                    // LOD distribution
                    ImGui::Text("LOD Distribution:");
                    uint32_t totalLOD = gpu.objectsLOD0 + gpu.objectsLOD1 + gpu.objectsLOD2 + gpu.objectsLOD3;
                    if (totalLOD > 0)
                    {
                        ImGui::Text("  LOD0 (High):   %u", gpu.objectsLOD0);
                        ImGui::Text("  LOD1 (Medium): %u", gpu.objectsLOD1);
                        ImGui::Text("  LOD2 (Low):    %u", gpu.objectsLOD2);
                        ImGui::Text("  LOD3 (Lowest): %u", gpu.objectsLOD3);
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "  No LOD data available");
                    }

                    ImGui::Separator();

                    // Meshlet culling stats (task shader level)
                    ImGui::Text("Meshlet Culling (Task Shader):");
                    if (gpu.totalMeshlets > 0)
                    {
                        ImGui::Text("  Total Meshlets:      %u", gpu.totalMeshlets);
                        ImGui::Text("  Culled by Frustum:   %u", gpu.meshletsCulledByFrustum);
                        ImGui::Text("  Culled by Backface:  %u", gpu.meshletsCulledByBackface);
                        ImGui::Text("  Visible:             %u", gpu.visibleMeshlets);

                        uint32_t totalCulled = gpu.meshletsCulledByFrustum + gpu.meshletsCulledByBackface;
                        float meshletCullRate = static_cast<float>(totalCulled) / static_cast<float>(gpu.totalMeshlets);
                        ImGui::Text("Meshlet Cull Rate:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(meshletCullRate, ImVec2(150, 0),
                                           (std::to_string(static_cast<int>(meshletCullRate * 100)) + "%").c_str());
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "  No meshlet data available");
                    }

                    ImGui::Separator();

                    // Merged buffer stats
                    ImGui::Text("Merged Buffer:");
                    ImGui::Text("  Vertices:  %u", gpu.mergedVertexCount);
                    ImGui::Text("  Indices:   %u", gpu.mergedIndexCount);
                    ImGui::Text("  Meshes:    %u", gpu.registeredMeshCount);
                    ImGui::Text("  Textures:  %u", gpu.registeredTextureCount);

                    if (gpu.hiZMipLevels > 0)
                    {
                        ImGui::Text("Hi-Z Mip Levels: %u", gpu.hiZMipLevels);
                    }

                    ImGui::Separator();

                    // Batch rendering stats
                    ImGui::Text("Indirect Batches:");
                    ImGui::Text("  Batches:           %u", gpu.batchCount);
                    ImGui::Text("  Commands/Batch:    %u", gpu.commandsPerBatch);
                    ImGui::Text("  Total Capacity:    %u objects", gpu.totalCapacity);
                    ImGui::Text("  Registered:        %u objects", gpu.totalObjects);
                    ImGui::Text("  Draw Calls:        %u", gpu.drawCalls);

                    if (gpu.totalCapacity > 0)
                    {
                        float utilization = static_cast<float>(gpu.totalObjects) / static_cast<float>(gpu.totalCapacity);
                        char percentStr[32];
                        if (utilization < 0.0001f && gpu.totalObjects > 0)
                        {
                            snprintf(percentStr, sizeof(percentStr), "<0.01%% (%u)", gpu.totalObjects);
                        }
                        else
                        {
                            snprintf(percentStr, sizeof(percentStr), "%.4f%%", utilization * 100.0f);
                        }
                        ImGui::Text("Capacity Used:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(utilization, ImVec2(150, 0), percentStr);
                    }

                    ImGui::Separator();

                    // Memory usage with capacity info
                    ImGui::Text("GPU Memory (Used / Allocated):");
                    auto formatMemory = [](uint64_t bytes) -> std::string
                    {
                        if (bytes >= 1024 * 1024 * 1024)
                        {
                            return std::to_string(bytes / (1024 * 1024 * 1024)) + "." +
                                   std::to_string((bytes / (1024 * 1024 * 100)) % 10) + " GB";
                        }
                        else if (bytes >= 1024 * 1024)
                        {
                            return std::to_string(bytes / (1024 * 1024)) + "." +
                                   std::to_string((bytes / (1024 * 100)) % 10) + " MB";
                        }
                        else if (bytes >= 1024)
                        {
                            return std::to_string(bytes / 1024) + " KB";
                        }
                        return std::to_string(bytes) + " B";
                    };

                    // Calculate used memory based on totalObjects
                    uint64_t usedDrawCmd = gpu.totalObjects * 20;  // sizeof(DrawIndexedIndirectCommand)
                    uint64_t usedPerDraw = gpu.totalObjects * 224; // sizeof(PerDrawData)
                    uint64_t usedTotal = usedDrawCmd + gpu.drawCountBufferSize + usedPerDraw;

                    ImGui::Text("  Draw Commands:  %s / %s",
                                formatMemory(usedDrawCmd).c_str(),
                                formatMemory(gpu.drawCommandBufferSize).c_str());
                    ImGui::Text("  Draw Counts:    %s (fixed)",
                                formatMemory(gpu.drawCountBufferSize).c_str());
                    ImGui::Text("  Per-Draw Data:  %s / %s",
                                formatMemory(usedPerDraw).c_str(),
                                formatMemory(gpu.perDrawDataBufferSize).c_str());
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                       "  Total:          %s / %s",
                                       formatMemory(usedTotal).c_str(),
                                       formatMemory(gpu.totalMemoryUsage).c_str());
                }

                ImGui::Unindent();
            }
            ImGui::Separator();

            // BVH Statistics
            if (ImGui::CollapsingHeader("BVH Statistics (CPU)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                ImGui::Text("Static BVH:  %zu entities, %zu nodes",
                            stats.staticBvhEntityCount, stats.staticBvhNodeCount);
                ImGui::Text("Dynamic BVH: %zu entities, %zu nodes",
                            stats.dynamicBvhEntityCount, stats.dynamicBvhNodeCount);
                ImGui::Text("Total:       %zu entities",
                            stats.staticBvhEntityCount + stats.dynamicBvhEntityCount);
                ImGui::Unindent();
            }
            ImGui::Separator();

            if (stats.cameraStats.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No cameras registered");
            }
            else
            {
                for (const auto& cam : stats.cameraStats)
                {
                    ImGui::PushID(static_cast<int>(cam.cameraId));

                    std::string header = "Camera " + std::to_string(cam.cameraId);
                    if (cam.isActive)
                    {
                        header += " [ACTIVE]";
                    }

                    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Indent();

                        ImGui::Text("Status:");
                        ImGui::SameLine();
                        ImGui::TextColored(cam.frustumReady ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                                           cam.frustumReady ? "Frustum Ready" : "Frustum Not Ready");

                        ImGui::SameLine();
                        ImGui::TextColored(cam.bvhBuilt ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0.5f, 0, 1),
                                           cam.bvhBuilt ? "| BVH Built" : "| BVH Not Built");

                        ImGui::Text("Occlusion:");
                        ImGui::SameLine();
                        if (!cam.occlusionEnabled)
                        {
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Disabled");
                        }
                        else if (!cam.occlusionInitialized)
                        {
                            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Not Initialized");
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Active");
                        }

                        ImGui::Separator();

                        ImGui::Text("Total Mesh Entities: %u", cam.totalMeshEntities);

                        if (cam.occlusionInitialized && cam.occlusionEnabled)
                        {
                            ImGui::Text("After Frustum Cull:  %u", cam.visibleAfterFrustumCull);
                            ImGui::Text("After Occlusion:     %u", cam.visibleAfterOcclusionCull);
                            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1), "Occluded: %u", cam.occludedCount);

                            if (cam.totalMeshEntities > 0)
                            {
                                float occlusionRate = static_cast<float>(cam.occludedCount) /
                                                      static_cast<float>(cam.totalMeshEntities);
                                ImGui::Text("Occlusion Rate:");
                                ImGui::SameLine();
                                ImGui::ProgressBar(occlusionRate, ImVec2(-1, 0),
                                                   (std::to_string(static_cast<int>(occlusionRate * 100)) + "%").c_str());
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Occlusion stats not available");
                        }

                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }
}
