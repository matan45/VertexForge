#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/animation/AnimationBudgetEvents.hpp"
#include <imgui.h>
#include <algorithm>

namespace windows
{
    void RenderConfigWindow::drawShadowSection()
    {
        if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Shadows", &settings.shadows.enabled))
            {
                markDirty();
            }

            if (settings.shadows.enabled)
            {
                ImGui::Spacing();
                drawShadowQualitySettings();
                drawShadowBiasSettings();
                drawShadowFilterSettings();
                drawDirectionalClipmapSettings();
            }

            drawRTShadowSection();
            drawShadowDebugSection();
            drawShadowStatistics();

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::drawShadowQualitySettings()
    {
        const char* qualityItems[] = {"Off", "Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)"};
        int currentQuality = static_cast<int>(settings.shadows.quality);
        if (ImGui::Combo("Quality", &currentQuality, qualityItems, 5))
        {
            settings.shadows.quality = static_cast<types::ShadowQuality>(currentQuality);
            markDirty();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click 'Apply' to change quality.\nThis may cause a brief stutter while the shadow atlas is resized.");
        }
    }

    void RenderConfigWindow::drawShadowBiasSettings()
    {
        ImGui::Separator();
        ImGui::Text("Bias Settings");
        ImGui::Spacing();

        if (ImGui::DragFloat("Depth Bias", &settings.shadows.shadowBias, 0.0001f, 0.0f, 0.1f, "%.4f"))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Constant depth offset to reduce shadow acne.");

        if (ImGui::DragFloat("Slope Bias", &settings.shadows.slopeBias, 0.01f, 0.0f, 5.0f, "%.2f"))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Slope-scaled bias for surfaces at grazing angles.\nHigher values reduce shadow acne on angled surfaces.");

        if (ImGui::DragFloat("Normal Bias", &settings.shadows.normalBias, 0.001f, 0.0f, 1.0f, "%.3f"))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Offset along surface normal to reduce peter-panning.\nHigher values push shadows away from caster.");
    }

    void RenderConfigWindow::drawShadowFilterSettings()
    {
        ImGui::Separator();
        ImGui::Text("Shadow Filtering");
        ImGui::Spacing();

        if (ImGui::Checkbox("Soft Shadows (PCSS)", &settings.shadows.softShadows))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable/disable PCSS contact-hardening soft shadows globally.\nWhen off, hard shadows are used (single tap).");

        // VK-1479 B1: page-binned directional shadow cull (experimental, default OFF).
        if (ImGui::Checkbox("Page-Binned Shadow Cull (experimental)", &settings.shadows.perViewCulling))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Directional VSM clipmap pages are drawn from a per-page GPU cull\n(off-screen casters recovered) instead of replaying the main draw list per page.\nExperimental; legacy path is the fallback when off.");


        if (ImGui::DragFloat("Light Size", &settings.shadows.globalLightSize, 0.01f, 0.01f, 10.0f))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Controls penumbra width for soft shadows (PCSS)");

        if (ImGui::DragFloat("Search Radius", &settings.shadows.searchRadiusMultiplier, 0.01f, 0.1f, 5.0f))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiplier for PCSS blocker search radius");

        ImGui::Separator();
        ImGui::Text("Shadow Darkness");
        ImGui::Spacing();

        if (ImGui::SliderFloat("Shadow Intensity", &settings.shadows.shadowIntensity, 0.0f, 1.0f, "%.2f"))
            markDirty();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Controls how dark shadowed areas are.\n"
                              "0.0 = Lighter shadows (ambient light in shadows)\n"
                              "1.0 = Darker shadows (no ambient in shadows)");
        }
    }

    void RenderConfigWindow::drawDirectionalClipmapSettings()
    {
        ImGui::Separator();
        ImGui::Text("Directional Clipmap (Sun)");
        ImGui::Spacing();

        int levels = static_cast<int>(settings.shadows.clipmapLevelCount);
        if (ImGui::SliderInt("Clipmap Levels", &levels, 2, 8))
        {
            settings.shadows.clipmapLevelCount = static_cast<uint32_t>(levels);
            markDirty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Concentric, camera-centered shadow shells for the directional light.\n"
                              "Each level covers 2x the area of the previous. More levels = farther\n"
                              "shadow reach. Takes effect on scene reload (resizes the page block).");

        if (ImGui::DragFloat("Base Extent (m)", &settings.shadows.clipmapBaseExtent, 1.0f, 4.0f, 256.0f, "%.0f"))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Half-size of the finest (level 0) shell in world units.\n"
                              "Smaller = sharper near shadows but more levels needed to reach the horizon.\n"
                              "Applies live on Apply.");

        if (ImGui::DragFloat("Depth Range (m)", &settings.shadows.clipmapDepthRange, 50.0f, 100.0f, 20000.0f, "%.0f"))
            markDirty();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far each shell spans along the sun direction.\n"
                              "Must cover scene height + view distance. Applies live on Apply.");
    }

    void RenderConfigWindow::drawShadowDebugSection()
    {
        ImGui::Separator();
        ImGui::Text("Debug Visualization");
        ImGui::Spacing();

        auto& dispatcher = events::EventDispatcher::instance();
        bool showShadowDebug = dispatcher.query(events::render::GetShowShadowDebugQuery{});

        if (ImGui::Checkbox("Show Shadow Frustums", &showShadowDebug))
        {
            events::render::SetShowShadowDebugCommand cmd;
            cmd.show = showShadowDebug;
            dispatcher.execute(cmd);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Visualize shadow map frustums:\n"
                              "- Directional: Cascade boxes (red->green)\n"
                              "- Spot: Perspective frustum (cyan)\n"
                              "- Point: Sphere radius (magenta)");
        }

    }

    void RenderConfigWindow::drawShadowStatistics()
    {
        ImGui::Separator();
        ImGui::Text("Statistics");
        ImGui::Spacing();

        auto& dispatcher = events::EventDispatcher::instance();
        auto shadowStats = dispatcher.query(events::render::GetShadowStatsQuery{});

        if (shadowStats.atlasWidth > 0)
        {
            ImGui::Text("Pool Dimensions: %ux%u", shadowStats.atlasWidth, shadowStats.atlasHeight);

            ImGui::Text("Pool Utilization:");
            ImGui::SameLine();
            char utilBuf[16];
            snprintf(utilBuf, sizeof(utilBuf), "%.1f%%", shadowStats.atlasUtilization * 100.0f);
            ImGui::ProgressBar(shadowStats.atlasUtilization, ImVec2(-1, 0), utilBuf);

            float poolMB = (shadowStats.atlasWidth * shadowStats.atlasHeight * 4) / (1024.0f * 1024.0f);
            ImGui::Text("Pool VRAM: %.1f MB", poolMB);
        }
        else
        {
            ImGui::TextDisabled("No shadow pool allocated");
        }

        if (shadowStats.activeShadowCasters > 0)
        {
            ImGui::Text("Active: %u casters, %u views",
                       shadowStats.activeShadowCasters, shadowStats.activeShadowViews);
            ImGui::TextDisabled("  Point: %u  Spot: %u",
                               shadowStats.pointLightCount,
                               shadowStats.spotLightCount);
        }

        ImGui::Spacing();
        ImGui::Text("Shadow Cache");
        if (shadowStats.totalStaticLights > 0)
        {
            ImGui::Text("  Static lights: %u", shadowStats.totalStaticLights);
            ImGui::Text("  View cache: %u cached, %u rendered, %u skipped",
                       shadowStats.cachedShadowMaps,
                       shadowStats.renderedThisFrame,
                       shadowStats.skippedThisFrame);
        }

        if (shadowStats.totalPages > 0)
        {
            ImGui::Text("  Pages: %u total, %u rendered, %u cached",
                       shadowStats.totalPages,
                       shadowStats.renderedPages,
                       shadowStats.cachedPages);

            float pageCacheRatio = static_cast<float>(shadowStats.cachedPages) /
                static_cast<float>(shadowStats.totalPages);
            ImVec4 cacheColor = pageCacheRatio > 0.5f ? ImVec4(0.3f, 1, 0.3f, 1)
                                                      : ImVec4(1, 0.8f, 0.2f, 1);
            ImGui::TextColored(cacheColor, "  Page cache hit: %.0f%%", pageCacheRatio * 100.0f);
        }

        if (shadowStats.totalPages > 0)
        {
            ImGui::Spacing();
            ImGui::Text("Dual-Layer Shadow");
            ImGui::Text("  Static pages rendered: %u", shadowStats.staticPagesRendered);
            ImGui::Text("  Dynamic pages rendered: %u", shadowStats.dynamicPagesRendered);
            ImGui::Text("  Tile copies/frame: %u", shadowStats.tileCopiesThisFrame);
            ImGui::Text("  Dynamic tiles allocated: %u", shadowStats.dynamicTilesAllocated);
        }

        if (!shadowStats.perLightInfo.empty() && ImGui::TreeNode("Per-Light Details"))
        {
            for (const auto& info : shadowStats.perLightInfo)
            {
                const char* typeNames[] = {"Directional", "Spot", "Point"};
                const char* typeName = (info.type < 3) ? typeNames[info.type] : "Unknown";

                ImGui::Text("%s (ID: %u)", typeName, info.entityId);
                ImGui::SameLine(200);
                ImGui::Text("Pages: %u alloc, %u dirty, %u cached",
                           info.pagesAllocated, info.pagesDirty, info.pagesCached);
            }
            ImGui::TreePop();
        }
    }

    void RenderConfigWindow::drawTerrainSection()
    {
        if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            auto& dispatcher = events::EventDispatcher::instance();

            if (ImGui::Checkbox("Enable Terrain Rendering", &settings.terrain.enabled))
            {
                markDirty();
                events::render::SetTerrainRenderingEnabledCommand cmd;
                cmd.enabled = settings.terrain.enabled;
                dispatcher.execute(cmd);
            }

            if (settings.terrain.enabled)
            {
                ImGui::Spacing();
                ImGui::Text("LOD Settings");
                ImGui::Spacing();

                if (ImGui::DragFloat("LOD Bias", &settings.terrain.lodBias, 0.1f, 0.1f, 5.0f, "%.1f"))
                {
                    markDirty();
                    events::render::SetTerrainLODBiasCommand cmd;
                    cmd.bias = settings.terrain.lodBias;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Controls terrain LOD selection.\nHigher = more detail, Lower = less detail.");

                if (ImGui::DragFloat("Error Threshold", &settings.terrain.errorThreshold, 0.1f, 0.5f, 10.0f, "%.1f"))
                {
                    markDirty();
                    events::render::SetTerrainErrorThresholdCommand cmd;
                    cmd.threshold = settings.terrain.errorThreshold;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Maximum allowed geometric error in pixels.\nLower = higher quality, Higher = better performance.");

                ImGui::Separator();
                ImGui::Text("Texture Settings");
                ImGui::Spacing();

                if (ImGui::DragFloat("Texture Scale", &settings.terrain.textureScale, 0.01f, 0.01f, 1.0f, "%.2f"))
                {
                    markDirty();
                    events::render::SetTerrainTextureScaleCommand cmd;
                    cmd.scale = settings.terrain.textureScale;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("UV scale for terrain textures.\nLower = larger texture tiles.");

                ImGui::Separator();
                ImGui::Text("Shadow Settings");
                ImGui::Spacing();

                if (ImGui::Checkbox("Cast Shadows", &settings.terrain.castShadows))
                {
                    markDirty();
                    events::scene::SetRenderSettingsCommand cmd;
                    cmd.settings = settings;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Terrain as a shadow CASTER (rasterized into shadow pages).\n"
                                      "Terrain still receives shadows when off.\n"
                                      "Off is a large GPU win on flat maps with negligible terrain self-shadowing.");
            }

            ImGui::Separator();
            ImGui::Text("RTT / Minimap Render Layers");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Render-layer index (0-31) for terrain and water. An RTT camera (e.g. the\n"
                                  "minimap) draws them only if its cullingMask has this bit set; the main\n"
                                  "viewport always renders all layers. Put terrain/water on a layer your\n"
                                  "minimap camera excludes to keep them off the minimap.");
            ImGui::Spacing();

            int terrainLayer = static_cast<int>(settings.terrain.renderLayer);
            if (ImGui::InputInt("Terrain Render Layer", &terrainLayer))
            {
                settings.terrain.renderLayer = static_cast<uint32_t>(std::clamp(terrainLayer, 0, 31));
                markDirty();
                events::scene::SetRenderSettingsCommand cmd;
                cmd.settings = settings;
                dispatcher.execute(cmd);
            }

            int waterLayer = static_cast<int>(settings.water.renderLayer);
            if (ImGui::InputInt("Water Render Layer", &waterLayer))
            {
                settings.water.renderLayer = static_cast<uint32_t>(std::clamp(waterLayer, 0, 31));
                markDirty();
                events::scene::SetRenderSettingsCommand cmd;
                cmd.settings = settings;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::drawVirtualTextureSection()
    {
        // VK-1209 — virtual texturing. Fields ride the whole RenderSettings struct; the
        // "Apply" button pushes them through the standard chain (applyVirtualTextureSettings).
        // Pool byte budgets are restart-scoped (Vulkan images don't resize live).
        if (ImGui::CollapsingHeader("Virtual Texturing"))
        {
            ImGui::Indent(10.0f);

            auto& vt = settings.virtualTexture;

            if (ImGui::Checkbox("Terrain RVT (bake splat composite to page atlas)", &vt.rvtEnabled))
                markDirty();
            if (ImGui::Checkbox("Material SVT (stream BC7 texture pages)", &vt.svtEnabled))
                markDirty();

            if (vt.rvtEnabled || vt.svtEnabled)
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Pool sizes apply after restart. Page budget / eviction apply live.");
                ImGui::Spacing();

                int rvtMB = static_cast<int>(vt.rvtPoolBudgetMB);
                if (ImGui::DragInt("RVT Pool (MB)", &rvtMB, 16, 32, 1024))
                {
                    vt.rvtPoolBudgetMB = static_cast<uint32_t>(rvtMB < 32 ? 32 : rvtMB);
                    markDirty();
                }
                int svtMB = static_cast<int>(vt.svtPoolBudgetMB);
                if (ImGui::DragInt("SVT Pool (MB)", &svtMB, 32, 64, 4096))
                {
                    vt.svtPoolBudgetMB = static_cast<uint32_t>(svtMB < 64 ? 64 : svtMB);
                    markDirty();
                }
                // VK-1480: page linear (Unorm) maps through a second BC7-Unorm atlas. Off = only
                // sRGB albedo/emission is paged; normal/ORM/height stay plain bindless (restart).
                if (ImGui::Checkbox("Page Linear Maps (normal/ORM via Unorm atlas)", &vt.svtPageLinearMaps))
                    markDirty();
                if (ImGui::DragFloat("RVT Texels / Meter", &vt.rvtTexelsPerMeter, 0.5f, 1.0f, 64.0f, "%.1f"))
                    markDirty();

                int perFrame = static_cast<int>(vt.pagesPerFrame);
                if (ImGui::DragInt("Pages / Frame", &perFrame, 1, 1, 256))
                {
                    vt.pagesPerFrame = static_cast<uint32_t>(perFrame < 1 ? 1 : perFrame);
                    markDirty();
                }
                int evictAge = static_cast<int>(vt.evictionAgeFrames);
                if (ImGui::DragInt("Eviction Age (frames)", &evictAge, 1, 1, 600))
                {
                    vt.evictionAgeFrames = static_cast<uint32_t>(evictAge < 1 ? 1 : evictAge);
                    markDirty();
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Press Apply to activate. RVT collapses per-fragment terrain\nsplat blending (up to 24 samples) into 2 atlas reads.");
            }

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::drawVFXLODSection()
    {
        if (ImGui::CollapsingHeader("VFX LOD"))
        {
            ImGui::Indent(10.0f);

            auto& dispatcher = events::EventDispatcher::instance();

            ImGui::Text("Distance Thresholds");
            ImGui::Spacing();

            bool changed = false;
            changed |= ImGui::SliderFloat("LOD 0 (Full)##vfx", &settings.vfxLOD.lod0Distance, 10.0f, 200.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 1 (Half)##vfx", &settings.vfxLOD.lod1Distance, 20.0f, 400.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 2 (Quarter)##vfx", &settings.vfxLOD.lod2Distance, 50.0f, 800.0f, "%.0f m");
            changed |= ImGui::SliderFloat("Transition Zone##vfx", &settings.vfxLOD.transitionZone, 1.0f, 50.0f, "%.0f m");

            if (settings.vfxLOD.lod1Distance <= settings.vfxLOD.lod0Distance)
                settings.vfxLOD.lod1Distance = settings.vfxLOD.lod0Distance + 1.0f;
            if (settings.vfxLOD.lod2Distance <= settings.vfxLOD.lod1Distance)
                settings.vfxLOD.lod2Distance = settings.vfxLOD.lod1Distance + 1.0f;

            if (changed)
            {
                markDirty();
                services::events::vfxruntime::SetVFXLODConfigCommand cmd;
                cmd.lod0Distance = settings.vfxLOD.lod0Distance;
                cmd.lod1Distance = settings.vfxLOD.lod1Distance;
                cmd.lod2Distance = settings.vfxLOD.lod2Distance;
                cmd.transitionZone = settings.vfxLOD.transitionZone;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::drawAnimationLODSection()
    {
        if (ImGui::CollapsingHeader("Animation LOD"))
        {
            ImGui::Indent(10.0f);

            auto& dispatcher = events::EventDispatcher::instance();
            bool changed = false;

            ImGui::Text("Distance Thresholds");
            ImGui::Spacing();
            changed |= ImGui::SliderFloat("LOD 0 Max##anim", &settings.animationLOD.lod0Distance, 5.0f, 50.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 1 Max##anim", &settings.animationLOD.lod1Distance, 25.0f, 150.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 2 Max##anim", &settings.animationLOD.lod2Distance, 50.0f, 300.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 3 Max##anim", &settings.animationLOD.lod3Distance, 100.0f, 500.0f, "%.0f m");

            if (settings.animationLOD.lod1Distance <= settings.animationLOD.lod0Distance)
                settings.animationLOD.lod1Distance = settings.animationLOD.lod0Distance + 1.0f;
            if (settings.animationLOD.lod2Distance <= settings.animationLOD.lod1Distance)
                settings.animationLOD.lod2Distance = settings.animationLOD.lod1Distance + 1.0f;
            if (settings.animationLOD.lod3Distance <= settings.animationLOD.lod2Distance)
                settings.animationLOD.lod3Distance = settings.animationLOD.lod2Distance + 1.0f;

            ImGui::Spacing();
            ImGui::Text("Update Intervals (frames)");
            ImGui::Spacing();

            ImGui::TextDisabled("LOD 0 Interval: 1 (every frame)");

            int intervals[2] = {
                static_cast<int>(settings.animationLOD.lod1Interval),
                static_cast<int>(settings.animationLOD.lod2Interval)
            };
            changed |= ImGui::SliderInt("LOD 1 Interval##anim", &intervals[0], 1, 4);
            changed |= ImGui::SliderInt("LOD 2 Interval##anim", &intervals[1], 2, 16);
            settings.animationLOD.lod1Interval = static_cast<uint32_t>(intervals[0]);
            settings.animationLOD.lod2Interval = static_cast<uint32_t>(intervals[1]);

            if (settings.animationLOD.lod1Interval < settings.animationLOD.lod0Interval)
                settings.animationLOD.lod1Interval = settings.animationLOD.lod0Interval;
            if (settings.animationLOD.lod2Interval < settings.animationLOD.lod1Interval)
                settings.animationLOD.lod2Interval = settings.animationLOD.lod1Interval;

            ImGui::Spacing();
            int maxInit = static_cast<int>(settings.animationLOD.maxStreamingInitPerFrame);
            if (ImGui::SliderInt("Max Streaming Init/Frame", &maxInit, 1, 16))
            {
                settings.animationLOD.maxStreamingInitPerFrame = static_cast<uint32_t>(maxInit);
                changed = true;
            }

            if (changed)
            {
                markDirty();
                services::events::animation::SetAnimationLODConfigCommand cmd;
                cmd.lod0Distance = settings.animationLOD.lod0Distance;
                cmd.lod1Distance = settings.animationLOD.lod1Distance;
                cmd.lod2Distance = settings.animationLOD.lod2Distance;
                cmd.lod3Distance = settings.animationLOD.lod3Distance;
                cmd.lod0Interval = settings.animationLOD.lod0Interval;
                cmd.lod1Interval = settings.animationLOD.lod1Interval;
                cmd.lod2Interval = settings.animationLOD.lod2Interval;
                cmd.maxStreamingInitPerFrame = settings.animationLOD.maxStreamingInitPerFrame;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::drawRTShadowSection()
    {
        if (ImGui::CollapsingHeader("Ray Traced Shadows"))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable RT Shadows", &settings.rtShadows.enabled))
                markDirty();

            if (settings.rtShadows.enabled)
            {
                ImGui::Spacing();
                ImGui::SeparatorText("Resolution");
                // VK-1430: shared resolution scale for directional + spot + point RT shadows. Half
                // traces+denoises at half res then edge-aware upsamples to full; Full is the default.
                const char* resItems[] = { "Full", "Half" };
                int resIndex = static_cast<int>(settings.rtShadows.shadowResolutionScale);
                if (ImGui::Combo("Shadow Resolution", &resIndex, resItems, IM_ARRAYSIZE(resItems)))
                {
                    settings.rtShadows.shadowResolutionScale =
                        static_cast<types::ShadowResolutionScale>(resIndex);
                    markDirty();
                }
                ImGui::TextDisabled("Half: ~2-4x faster trace+denoise, joint-bilateral upsampled.");

                ImGui::Spacing();
                ImGui::SeparatorText("Ray Parameters");
                if (ImGui::SliderFloat("Max Ray Distance", &settings.rtShadows.maxRayDistance, 50.0f, 2000.0f, "%.0f"))
                    markDirty();
                if (ImGui::SliderFloat("RT Normal Bias", &settings.rtShadows.normalBias, 0.001f, 0.2f, "%.4f"))
                    markDirty();
                if (ImGui::SliderFloat("Ray T-Min", &settings.rtShadows.rayTMin, 0.001f, 10.0f, "%.4f"))
                    markDirty();

                ImGui::Spacing();
                ImGui::SeparatorText("Denoiser - Temporal");
                if (ImGui::SliderFloat("Temporal Blend", &settings.rtShadows.temporalBlend, 0.0f, 0.99f, "%.2f"))
                    markDirty();
                if (ImGui::SliderFloat("Depth Threshold", &settings.rtShadows.depthThreshold, 0.001f, 0.1f, "%.4f"))
                    markDirty();
                if (ImGui::SliderFloat("Normal Threshold", &settings.rtShadows.normalThreshold, 0.5f, 1.0f, "%.2f"))
                    markDirty();

                ImGui::Spacing();
                ImGui::SeparatorText("Denoiser - Spatial");
                if (ImGui::SliderFloat("Phi Depth", &settings.rtShadows.spatialPhiDepth, 0.001f, 0.05f, "%.4f"))
                    markDirty();
                if (ImGui::SliderFloat("Phi Normal", &settings.rtShadows.spatialPhiNormal, 1.0f, 128.0f, "%.1f"))
                    markDirty();
                if (ImGui::SliderInt("Spatial Passes", &settings.rtShadows.spatialPasses, 1, 5))
                    markDirty();

                ImGui::Spacing();
                ImGui::SeparatorText("Adaptive Budget");
                if (ImGui::Checkbox("Enable Adaptive Budget", &settings.rtShadows.adaptiveBudgetEnabled))
                    markDirty();
                if (settings.rtShadows.adaptiveBudgetEnabled)
                {
                    if (ImGui::SliderFloat("Budget (ms)", &settings.rtShadows.budgetMs, 0.5f, 8.0f, "%.1f"))
                        markDirty();
                    if (ImGui::SliderFloat("AS Memory Budget (MB)", &settings.rtShadows.asMemoryBudgetMB, 64.0f, 1024.0f, "%.0f"))
                        markDirty();
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Performance");
                auto& dispatcher = events::EventDispatcher::instance();
                auto rtStats = dispatcher.query(events::render::GetRTShadowStatsQuery{});

                float ratio = rtStats.budgetMs > 0.0f ? rtStats.totalRTShadowMs / rtStats.budgetMs : 0.0f;
                ImVec4 color = ratio < 0.7f ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                             : ratio < 1.0f ? ImVec4(0.9f, 0.8f, 0.1f, 1.0f)
                                            : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
                ImGui::TextColored(color, "Total: %.2f ms (budget: %.1f ms)",
                                   rtStats.totalRTShadowMs, rtStats.budgetMs);
                ImGui::Text("  Ray dispatch: %.2f ms", rtStats.rayDispatchMs);
                ImGui::Text("  Denoiser: %.2f ms", rtStats.denoiserMs);

                if (rtStats.isThrottled)
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.1f, 1.0f),
                        "  [THROTTLED] ray dist=%.0f, passes=%d",
                        rtStats.currentMaxRayDistance, rtStats.currentSpatialPasses);
                }

                ImGui::Spacing();
                float asMB = static_cast<float>(rtStats.blasTotalBytes + rtStats.tlasTotalBytes) / (1024.0f * 1024.0f);
                ImGui::Text("AS Memory: %.1f MB (%u BLAS, %u instances)",
                            asMB, rtStats.blasCount, rtStats.tlasInstanceCount);
                if (rtStats.asMemoryOverBudget)
                    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "  [OVER BUDGET]");

                float scratchMB = static_cast<float>(rtStats.scratchPeakBytes) / (1024.0f * 1024.0f);
                ImGui::Text("Scratch peak: %.1f MB", scratchMB);
                ImGui::Text("BLAS build: %.2f ms | TLAS build: %.2f ms",
                            rtStats.blasBuildMs, rtStats.tlasBuildMs);
            }

            // Spot lights (VK-1175): independent opt-in RT override layered on the spot VSM base.
            // Reuses the ray/denoiser tunables above; the closest/brightest spotBudget spots get RT.
            ImGui::Spacing();
            ImGui::SeparatorText("Spot Lights");
            if (ImGui::Checkbox("RT Spot Shadows", &settings.rtShadows.spotEnabled))
                markDirty();
            if (settings.rtShadows.spotEnabled)
            {
                int budget = static_cast<int>(settings.rtShadows.spotBudget);
                if (ImGui::SliderInt("RT Spot Budget", &budget, 1, 8))
                {
                    settings.rtShadows.spotBudget = static_cast<uint32_t>(budget);
                    markDirty();
                }
                ImGui::TextDisabled("Closest/brightest spots get RT; the rest stay on VSM.");
            }

            // Point lights (VK-1176): independent opt-in RT override layered on the point VSM base.
            // Reuses the ray/denoiser tunables above; the closest/brightest pointBudget points get RT.
            ImGui::Spacing();
            ImGui::SeparatorText("Point Lights");
            if (ImGui::Checkbox("RT Point Shadows", &settings.rtShadows.pointEnabled))
                markDirty();
            if (settings.rtShadows.pointEnabled)
            {
                int budget = static_cast<int>(settings.rtShadows.pointBudget);
                if (ImGui::SliderInt("RT Point Budget", &budget, 1, 8))
                {
                    settings.rtShadows.pointBudget = static_cast<uint32_t>(budget);
                    markDirty();
                }
                ImGui::TextDisabled("Closest/brightest points get RT; the rest stay on VSM.");
            }

            ImGui::Unindent(10.0f);
        }
    }
}
