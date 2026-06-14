#include "CullingStatsWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include <imgui.h>
#include <string>

namespace windows
{
    void CullingStatsWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Culling Stats", &visible))
        {
            events::render::GetCullingStatsQuery query;
            auto stats = events::EventDispatcher::instance().query(query);

            ImGui::Text("Active Camera: %u", stats.activeCameraId);
            ImGui::Separator();

            if (ImGui::CollapsingHeader("GPU Pipeline Status", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                drawGPUPipelineStatus();
                ImGui::Unindent();
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("GPU-Driven Rendering", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                const auto& gpu = stats.gpuDriven;

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

                    ImGui::Text("Objects: %u total", gpu.totalObjects);
                    if (gpu.totalObjects > 0)
                    {
                        ImGui::Text("  Culled by Distance:  %u", gpu.culledByDistance);
                        ImGui::Text("  Culled by Frustum:   %u", gpu.culledByFrustum);
                        ImGui::Text("  Culled by Occlusion: %u", gpu.culledByOcclusion);
                        ImGui::Text("  Visible:             %u", gpu.visibleObjects);

                        uint32_t totalCulled = gpu.culledByDistance + gpu.culledByFrustum + gpu.culledByOcclusion;
                        float cullRate = static_cast<float>(totalCulled) / static_cast<float>(gpu.totalObjects);
                        ImGui::Text("Cull Rate:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(cullRate, ImVec2(150, 0),
                                           (std::to_string(static_cast<int>(cullRate * 100)) + "%").c_str());
                    }

                    ImGui::Separator();

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

                    ImGui::Text("Meshlet Culling (Task Shader):");
                    if (gpu.totalMeshlets > 0)
                    {
                        ImGui::Text("  Total Meshlets:      %u", gpu.totalMeshlets);
                        ImGui::Text("  Culled by Frustum:   %u", gpu.meshletsCulledByFrustum);
                        ImGui::Text("  Culled by Backface:  %u", gpu.meshletsCulledByBackface);
                        ImGui::Text("  Culled by Occlusion: %u", gpu.meshletsCulledByOcclusion);
                        ImGui::Text("  Visible:             %u", gpu.visibleMeshlets);

                        uint32_t totalCulled = gpu.meshletsCulledByFrustum + gpu.meshletsCulledByBackface + gpu.meshletsCulledByOcclusion;
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

                    ImGui::Text("Light Culling:");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.bvhLightCullingEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "BVH");
                    ImGui::SameLine();
                    ImGui::TextColored(gpu.hiZLightOcclusionEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                                       "| Hi-Z Occlusion");

                    if (gpu.totalLights > 0)
                    {
                        ImGui::Text("  Total Lights:        %u", gpu.totalLights);
                        ImGui::Text("  After BVH Cull:      %u", gpu.lightsAfterBVHCull);
                        ImGui::Text("  After Hi-Z Cull:     %u", gpu.lightsAfterHiZCull);
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1), "  Culled by BVH:       %u", gpu.lightsCulledByBVH);
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1), "  Culled by Hi-Z:      %u", gpu.lightsCulledByHiZ);

                        uint32_t totalCulled = gpu.lightsCulledByBVH + gpu.lightsCulledByHiZ;
                        float lightCullRate = static_cast<float>(totalCulled) / static_cast<float>(gpu.totalLights);
                        ImGui::Text("Light Cull Rate:");
                        ImGui::SameLine();
                        ImGui::ProgressBar(lightCullRate, ImVec2(150, 0),
                                           (std::to_string(static_cast<int>(lightCullRate * 100)) + "%").c_str());
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "  No lights in scene");
                    }

                    ImGui::Separator();

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

                    uint64_t usedDrawCmd = gpu.totalObjects * 20;
                    uint64_t usedPerDraw = gpu.totalObjects * 224;
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

            if (ImGui::CollapsingHeader("Terrain Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                const auto& t = stats.terrain;

                ImGui::Text("CPU Frame Timings:");
                ImGui::Text("  updateTerrain:   %.1f us (%.2f ms)", t.updateTerrainUs, t.updateTerrainUs / 1000.0f);
                ImGui::Text("  Streaming:       %.1f us (%.2f ms)", t.streamingUs, t.streamingUs / 1000.0f);
                ImGui::Text("  buildTileData:   %.1f us (%.2f ms)", t.buildTileDataUs, t.buildTileDataUs / 1000.0f);
                ImGui::Text("  uploadTileData:  %.1f us (%.2f ms)", t.uploadTileDataUs, t.uploadTileDataUs / 1000.0f);

                ImGui::Separator();

                ImGui::Text("GPU Culling (Task Shader):");
                ImGui::Text("  Total Tiles:       %u", t.totalTiles);
                ImGui::Text("  Culled Tiles:      %u", t.culledTiles);
                ImGui::Text("  Total Meshlets:    %u", t.totalMeshlets);
                ImGui::Text("  Culled Meshlets:   %u", t.culledMeshlets);
                ImGui::Text("  Culled by Occl.:   %u", t.culledByOcclusion);
                ImGui::Text("  Visible Meshlets:  %u", t.visibleMeshlets);

                if (t.totalMeshlets > 0)
                {
                    float cullRate = static_cast<float>(t.culledMeshlets) / static_cast<float>(t.totalMeshlets);
                    ImGui::Text("  Meshlet Cull Rate:");
                    ImGui::SameLine();
                    ImGui::ProgressBar(cullRate, ImVec2(150, 0),
                                       (std::to_string(static_cast<int>(cullRate * 100)) + "%").c_str());
                }

                ImGui::Separator();

                ImGui::Text("LOD Distribution:");
                ImGui::Text("  LOD0: %u  LOD1: %u  LOD2: %u  LOD3: %u  LOD4: %u  LOD5: %u",
                            t.lodCount0, t.lodCount1, t.lodCount2, t.lodCount3, t.lodCount4, t.lodCount5);

                ImGui::Separator();

                ImGui::Text("Streaming:");
                ImGui::Text("  Tiles Loaded:    %u", t.tilesLoaded);
                ImGui::Text("  Streaming:       %u", t.tilesStreaming);
                ImGui::Text("  Full Detail:     %u", t.fullDetailTiles);
                ImGui::Text("  Fallback Only:   %u", t.fallbackTiles);
                ImGui::Text("  Uploads/Frame:   %u", t.uploadsThisFrame);

                if (t.bytesUploadedThisFrame > 0)
                {
                    ImGui::Text("  Bytes/Frame:     %zu KB", t.bytesUploadedThisFrame / 1024);
                }

                if (t.memoryBudgetBytes > 0)
                {
                    float memUsage = static_cast<float>(t.memoryUsedBytes) / static_cast<float>(t.memoryBudgetBytes);
                    ImGui::Text("  GPU Memory:");
                    ImGui::SameLine();
                    char memStr[64];
                    snprintf(memStr, sizeof(memStr), "%zu / %zu MB",
                             t.memoryUsedBytes / (1024 * 1024), t.memoryBudgetBytes / (1024 * 1024));
                    ImGui::ProgressBar(memUsage, ImVec2(150, 0), memStr);
                }

                ImGui::Unindent();
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Texture Mip Streaming"))
            {
                ImGui::Indent();
                const auto& ts = stats.textureStream;

                ImGui::Text("Registered:    %u (%u full, %u partial)",
                            ts.totalRegistered, ts.fullyLoaded, ts.partiallyLoaded);
                ImGui::Text("Pending Reads: %u", ts.pendingReads);
                ImGui::Text("Uploads/Frame: %u", ts.uploadsThisFrame);
                ImGui::Text("Evictions:     %u", ts.evictionsThisFrame);

                if (ts.bytesUploadedThisFrame > 0)
                {
                    ImGui::Text("Bytes/Frame:   %zu KB", ts.bytesUploadedThisFrame / 1024);
                }

                if (ts.vramBudgetBytes > 0)
                {
                    float memUsage = static_cast<float>(ts.vramUsedBytes) / static_cast<float>(ts.vramBudgetBytes);
                    ImGui::Text("VRAM:");
                    ImGui::SameLine();
                    char memStr[64];
                    snprintf(memStr, sizeof(memStr), "%zu / %zu MB",
                             ts.vramUsedBytes / (1024 * 1024), ts.vramBudgetBytes / (1024 * 1024));
                    ImGui::ProgressBar(memUsage, ImVec2(150, 0), memStr);
                }

                ImGui::Unindent();
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Water/Ocean Performance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                const auto& w = stats.water;

                float totalUs = w.readbackUs + w.dispatchUs + w.updateUs + w.renderUs;
                ImGui::Text("CPU Frame Timings:");
                ImGui::Text("  Readback (half->float): %.1f us (%.2f ms)", w.readbackUs, w.readbackUs / 1000.0f);
                ImGui::Text("  FFT Dispatch (cmd rec): %.1f us (%.2f ms)", w.dispatchUs, w.dispatchUs / 1000.0f);
                ImGui::Text("  Update (tile data):     %.1f us (%.2f ms)", w.updateUs, w.updateUs / 1000.0f);
                ImGui::Text("  Render (draw cmd):      %.1f us (%.2f ms)", w.renderUs, w.renderUs / 1000.0f);
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                                   "  Total CPU:              %.1f us (%.2f ms)", totalUs, totalUs / 1000.0f);

                ImGui::Unindent();
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("BVH Statistics (CPU)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();

                ImGui::Text("Mesh BVH:");
                ImGui::Text("  Static:  %zu entities, %zu nodes",
                            stats.staticBvhEntityCount, stats.staticBvhNodeCount);
                ImGui::Text("  Dynamic: %zu entities, %zu nodes",
                            stats.dynamicBvhEntityCount, stats.dynamicBvhNodeCount);
                ImGui::Text("  Total:   %zu entities",
                            stats.staticBvhEntityCount + stats.dynamicBvhEntityCount);

                ImGui::Separator();

                ImGui::Text("Light BVH:");
                ImGui::Text("  Static:  %zu lights, %zu nodes",
                            stats.staticLightBvhCount, stats.staticLightBvhNodeCount);
                ImGui::Text("  Dynamic: %zu lights, %zu nodes",
                            stats.dynamicLightBvhCount, stats.dynamicLightBvhNodeCount);
                ImGui::Text("  Total:   %zu lights",
                            stats.staticLightBvhCount + stats.dynamicLightBvhCount);

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

                        ImGui::Separator();

                        ImGui::Text("Total Mesh Entities: %u", cam.totalMeshEntities);

                        ImGui::Unindent();
                    }

                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }

    void CullingStatsWindow::drawGPUPipelineStatus()
    {
        auto p = events::EventDispatcher::instance().query(events::render::GetGPUPipelineStatusQuery{});

        ImGui::Text("Async Compute Queue:");
        ImGui::SameLine();
        ImGui::TextColored(p.asyncComputeEnabled ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1),
                           p.asyncComputeEnabled ? "ACTIVE" : "DISABLED");
        if (p.asyncComputeEnabled)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "(family %u)", p.asyncComputeQueueFamily);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1),
                "  Passes: Light Culling, Grass, GI, Atmosphere, Clouds, VFX");
        }
        ImGui::Separator();
        ImGui::Text("Shadow Recording:");
        ImGui::SameLine();
        ImGui::TextColored(p.parallelShadowRecording ? ImVec4(0, 1, 0, 1) : ImVec4(0.8f, 0.8f, 0, 1),
                           p.parallelShadowRecording ? "PARALLEL" : "INLINE");
        if (p.shadowTileCount > 0)
        {
            ImGui::Text("  Tiles: %u | Threads: %u | CPU: %.1f us (%.2f ms)",
                        p.shadowTileCount, p.shadowThreadsUsed,
                        p.shadowRecordingUs, p.shadowRecordingUs / 1000.0f);
            if (p.parallelShadowRecording && p.shadowThreadsUsed > 0)
                ImGui::Text("  Avg per thread: %.1f us (%u tiles/thread)",
                            p.shadowRecordingUs / static_cast<float>(p.shadowThreadsUsed),
                            p.shadowTileCount / p.shadowThreadsUsed);
        }
        ImGui::Separator();
        ImGui::Text("Scene Recording:");
        ImGui::SameLine();
        ImGui::TextColored(p.parallelSceneRecording ? ImVec4(0, 1, 0, 1) : ImVec4(0.8f, 0.8f, 0, 1),
                           p.parallelSceneRecording ? "PARALLEL" : "INLINE");
        if (p.parallelSceneRecording && p.sceneSecondaryCount > 0)
        {
            ImGui::Text("  Secondary buffers: %u | CPU: %.1f us (%.2f ms)",
                        p.sceneSecondaryCount, p.sceneRecordingUs, p.sceneRecordingUs / 1000.0f);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1),
                "  Groups: Meshes, Terrain, Grass, Water+Billboards");
        }
        ImGui::Separator();
        ImGui::Text("Worker Threads: %u", p.workerThreadCount);
    }
}
