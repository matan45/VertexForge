#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/animation/AnimationBudgetEvents.hpp"
#include <imgui.h>

namespace windows
{
    void RenderConfigWindow::drawShadowSection()
    {
        if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            if (ImGui::Checkbox("Enable Shadows", &settings.shadows.enabled))
            {
                isDirty = true;
            }

            if (settings.shadows.enabled)
            {
                ImGui::Spacing();
                drawShadowQualitySettings();
                drawShadowCSMSettings();
                drawShadowBiasSettings();
                drawShadowFilterSettings();
            }

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
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click 'Apply' to change quality.\nThis may cause a brief stutter while the shadow atlas is resized.");
        }
    }

    void RenderConfigWindow::drawShadowCSMSettings()
    {
        ImGui::Separator();
        ImGui::Text("Directional Light");
        ImGui::Spacing();

        const char* dirModes[] = {"CSM (Cascaded)", "Clipmap"};
        int currentDirMode = static_cast<int>(settings.shadows.directionalMode);
        if (ImGui::Combo("Shadow Mode", &currentDirMode, dirModes, 2))
        {
            settings.shadows.directionalMode = static_cast<types::DirectionalShadowMode>(currentDirMode);
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("CSM: Traditional cascaded shadow maps (4 cascades max)\n"
                              "Clipmap: Concentric shadow levels for large-scale worlds");
        }

        ImGui::Spacing();

        if (settings.shadows.directionalMode == types::DirectionalShadowMode::CSM)
        {
            int cascades = settings.shadows.cascadeCount;
            if (ImGui::SliderInt("Cascade Count", &cascades, 1, 4))
            {
                settings.shadows.cascadeCount = static_cast<uint8_t>(cascades);
                isDirty = true;
            }

            const char* splitModes[] = {"Linear", "Logarithmic", "Practical"};
            int currentMode = static_cast<int>(settings.shadows.cascadeSplitMode);
            if (ImGui::Combo("Split Mode", &currentMode, splitModes, 3))
            {
                settings.shadows.cascadeSplitMode = static_cast<types::CascadeSplitMode>(currentMode);
                isDirty = true;
            }
        }
        else
        {
            int levels = settings.shadows.clipmapLevelCount;
            if (ImGui::SliderInt("Level Count", &levels, 4, 16))
            {
                settings.shadows.clipmapLevelCount = static_cast<uint8_t>(levels);
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Number of clipmap levels.\n"
                                  "Each level doubles world coverage.\n"
                                  "16 levels = ~65km range with 2m base extent.");
            }

            if (ImGui::DragFloat("Base Extent (m)", &settings.shadows.clipmapBaseExtent, 0.1f, 0.5f, 10.0f, "%.1f"))
            {
                isDirty = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("World half-extent of level 0 (finest detail).\n"
                                  "Smaller = higher near-shadow quality.");
            }
        }
    }

    void RenderConfigWindow::drawShadowBiasSettings()
    {
        ImGui::Separator();
        ImGui::Text("Bias Settings");
        ImGui::Spacing();

        if (ImGui::DragFloat("Depth Bias", &settings.shadows.shadowBias, 0.0001f, 0.0f, 0.1f, "%.4f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Constant depth offset to reduce shadow acne.");

        if (ImGui::DragFloat("Slope Bias", &settings.shadows.slopeBias, 0.01f, 0.0f, 5.0f, "%.2f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Slope-scaled bias for surfaces at grazing angles.\nHigher values reduce shadow acne on angled surfaces.");

        if (ImGui::DragFloat("Normal Bias", &settings.shadows.normalBias, 0.001f, 0.0f, 1.0f, "%.3f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Offset along surface normal to reduce peter-panning.\nHigher values push shadows away from caster.");
    }

    void RenderConfigWindow::drawShadowFilterSettings()
    {
        ImGui::Separator();
        ImGui::Text("Shadow Filtering");
        ImGui::Spacing();

        if (ImGui::Checkbox("Soft Shadows (PCSS)", &settings.shadows.softShadows))
            isDirty = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable/disable PCSS contact-hardening soft shadows globally.\nWhen off, hard shadows are used (single tap).");

        ImGui::Separator();
        ImGui::Text("Shadow Darkness");
        ImGui::Spacing();

        if (ImGui::SliderFloat("Shadow Intensity", &settings.shadows.shadowIntensity, 0.0f, 1.0f, "%.2f"))
            isDirty = true;
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Controls how dark shadowed areas are.\n"
                              "0.0 = Lighter shadows (ambient light in shadows)\n"
                              "1.0 = Darker shadows (no ambient in shadows)");
        }
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
            ImGui::ProgressBar(shadowStats.atlasUtilization, ImVec2(-1, 0),
                (std::to_string(static_cast<int>(shadowStats.atlasUtilization * 100)) + "%%").c_str());

            float poolMB = (shadowStats.atlasWidth * shadowStats.atlasHeight * 4) / (1024.0f * 1024.0f);
            uint32_t pointRes = shadowStats.pointResolution;
            float cubeMB = shadowStats.pointLightCount * 6 * pointRes * pointRes * 4 / (1024.0f * 1024.0f);
            float totalMB = poolMB + cubeMB;
            ImGui::Text("Pool VRAM: %.1f MB (Pool: %.1f, Cubes: %.1f)", totalMB, poolMB, cubeMB);
        }
        else
        {
            ImGui::TextDisabled("No shadow pool allocated");
        }

        if (shadowStats.activeShadowCasters > 0)
        {
            ImGui::Text("Active: %u casters, %u views",
                       shadowStats.activeShadowCasters, shadowStats.activeShadowViews);
            ImGui::TextDisabled("  Dir: %u  Point: %u  Spot: %u",
                               shadowStats.directionalLightCount,
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
    }

    void RenderConfigWindow::drawTerrainSection()
    {
        if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            auto& dispatcher = events::EventDispatcher::instance();

            if (ImGui::Checkbox("Enable Terrain Rendering", &settings.terrain.enabled))
            {
                isDirty = true;
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
                    isDirty = true;
                    events::render::SetTerrainLODBiasCommand cmd;
                    cmd.bias = settings.terrain.lodBias;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Controls terrain LOD selection.\nHigher = more detail, Lower = less detail.");

                if (ImGui::DragFloat("Error Threshold", &settings.terrain.errorThreshold, 0.1f, 0.5f, 10.0f, "%.1f"))
                {
                    isDirty = true;
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
                    isDirty = true;
                    events::render::SetTerrainTextureScaleCommand cmd;
                    cmd.scale = settings.terrain.textureScale;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("UV scale for terrain textures.\nLower = larger texture tiles.");

                ImGui::Separator();
                ImGui::Text("Shadow Settings");
                ImGui::Spacing();

                int shadowLOD = static_cast<int>(settings.terrain.shadowLOD);
                if (ImGui::SliderInt("Shadow LOD", &shadowLOD, 0, 3))
                {
                    settings.terrain.shadowLOD = static_cast<uint32_t>(shadowLOD);
                    isDirty = true;
                    events::render::SetTerrainShadowLODCommand cmd;
                    cmd.lod = settings.terrain.shadowLOD;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("LOD level used for terrain shadow rendering.\n"
                                      "0 = Highest detail (slowest)\n"
                                      "3 = Lowest detail (fastest)\n"
                                      "Recommended: 2 (shadows don't need high detail)");
                }

                ImGui::Separator();
                ImGui::Text("Virtual Texturing");
                ImGui::Spacing();

                if (ImGui::Checkbox("Enable SVT", &settings.terrain.svtEnabled))
                {
                    isDirty = true;
                    events::render::SetTerrainSVTEnabledCommand cmd;
                    cmd.enabled = settings.terrain.svtEnabled;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Sparse Virtual Texturing (SVT)\n"
                                      "Streams only visible texture tiles to GPU.\n"
                                      "Reduces VRAM usage for large terrains.");
                }
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
                isDirty = true;
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
                isDirty = true;
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
}
