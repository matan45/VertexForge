#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/SceneEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/animation/AnimationBudgetEvents.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace windows
{
    void RenderConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadFromScene();
        }
    }

    void RenderConfigWindow::loadFromScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::GetRenderSettingsQuery query;
        settings = dispatcher.query(query);

        // Validate terrain settings (may be uninitialized in old scene files)
        if (!std::isfinite(settings.terrain.lodBias) || settings.terrain.lodBias < 0.1f || settings.terrain.lodBias > 10.0f)
        {
            settings.terrain.lodBias = 1.0f;
        }
        if (!std::isfinite(settings.terrain.errorThreshold) || settings.terrain.errorThreshold < 0.1f || settings.terrain.errorThreshold > 20.0f)
        {
            settings.terrain.errorThreshold = 2.0f;
        }
        if (!std::isfinite(settings.terrain.textureScale) || settings.terrain.textureScale < 0.001f || settings.terrain.textureScale > 10.0f)
        {
            settings.terrain.textureScale = 0.1f;
        }
        if (settings.terrain.shadowLOD > 3)
        {
            settings.terrain.shadowLOD = 2;
        }

        settingsLoaded = true;
        isDirty = false;
    }

    void RenderConfigWindow::saveToScene()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::SetRenderSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);
        isDirty = false;
    }

    void RenderConfigWindow::resetToDefaults()
    {
        settings = types::RenderSettings::createDefault();
        isDirty = true;
    }

    void RenderConfigWindow::applySettings()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::SetRenderSettingsCommand sceneCmd;
        sceneCmd.settings = settings;
        dispatcher.execute(sceneCmd);

        events::render::ApplyShadowSettingsCommand shadowCmd;
        shadowCmd.settings = settings;
        dispatcher.execute(shadowCmd);

        {
            services::events::vfxruntime::SetVFXLODConfigCommand cmd;
            cmd.lod0Distance = settings.vfxLOD.lod0Distance;
            cmd.lod1Distance = settings.vfxLOD.lod1Distance;
            cmd.lod2Distance = settings.vfxLOD.lod2Distance;
            cmd.transitionZone = settings.vfxLOD.transitionZone;
            dispatcher.execute(cmd);
        }

        {
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
    }

    void RenderConfigWindow::drawShadowQualitySettings()
    {
        const char* qualityItems[] = {"Off", "Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)"};
        int currentQuality = static_cast<int>(settings.shadows.quality);
        if (ImGui::Combo("Quality", &currentQuality, qualityItems, 5))
        {
            settings.shadows.quality = static_cast<types::ShadowQuality>(currentQuality);
            settings.shadows.atlas = types::ShadowAtlasConfig::fromQuality(settings.shadows.quality);
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
        ImGui::Text("Directional Light (CSM)");
        ImGui::Spacing();

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

    void RenderConfigWindow::drawShadowBiasSettings()
    {
        ImGui::Separator();
        ImGui::Text("Bias Settings");
        ImGui::Spacing();

        if (ImGui::DragFloat("Depth Bias", &settings.shadows.shadowBias, 0.0001f, 0.0f, 0.1f, "%.4f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Constant depth offset to reduce shadow acne.");
        }
        if (ImGui::DragFloat("Slope Bias", &settings.shadows.slopeBias, 0.01f, 0.0f, 5.0f, "%.2f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Slope-scaled bias for surfaces at grazing angles.\nHigher values reduce shadow acne on angled surfaces.");
        }
        if (ImGui::DragFloat("Normal Bias", &settings.shadows.normalBias, 0.001f, 0.0f, 1.0f, "%.3f"))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Offset along surface normal to reduce peter-panning.\nHigher values push shadows away from caster.");
        }
    }

    void RenderConfigWindow::drawShadowFilterSettings()
    {
        ImGui::Separator();
        ImGui::Text("Shadow Filtering");
        ImGui::Spacing();

        const char* kernelItems[] = {"1x1 (Hard)", "3x3", "5x5"};
        int kernelIdx = 0;
        switch (settings.shadows.pcfKernelSize)
        {
            case types::PCFKernelSize::x1: kernelIdx = 0; break;
            case types::PCFKernelSize::x3: kernelIdx = 1; break;
            case types::PCFKernelSize::x5: kernelIdx = 2; break;
        }
        if (ImGui::Combo("PCF Kernel", &kernelIdx, kernelItems, 3))
        {
            switch (kernelIdx)
            {
                case 0: settings.shadows.pcfKernelSize = types::PCFKernelSize::x1; break;
                case 1: settings.shadows.pcfKernelSize = types::PCFKernelSize::x3; break;
                case 2: settings.shadows.pcfKernelSize = types::PCFKernelSize::x5; break;
            }
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("PCF kernel size for soft shadow edges.\nLarger = softer but slower.");
        }

        if (ImGui::Checkbox("Soft Shadows", &settings.shadows.softShadowsEnabled))
        {
            isDirty = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Enable/disable PCF shadow filtering globally.");
        }

        ImGui::Separator();
        ImGui::Text("Shadow Darkness");
        ImGui::Spacing();

        if (ImGui::SliderFloat("Shadow Intensity", &settings.shadows.shadowIntensity, 0.0f, 1.0f, "%.2f"))
        {
            isDirty = true;
        }
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
            ImGui::Text("Atlas: %ux%u", shadowStats.atlasWidth, shadowStats.atlasHeight);

            ImGui::Text("Utilization:");
            ImGui::SameLine();
            ImGui::ProgressBar(shadowStats.atlasUtilization, ImVec2(-1, 0),
                (std::to_string(static_cast<int>(shadowStats.atlasUtilization * 100)) + "%%").c_str());

            float atlasMB = (shadowStats.atlasWidth * shadowStats.atlasHeight * 4) / (1024.0f * 1024.0f);

            uint32_t pointRes = shadowStats.pointResolution;
            float cubeMB = shadowStats.pointLightCount * 6 * pointRes * pointRes * 4 / (1024.0f * 1024.0f);

            float totalMB = atlasMB + cubeMB;
            ImGui::Text("Est. VRAM: %.1f MB (Atlas: %.1f, Cubes: %.1f)",
                       totalMB, atlasMB, cubeMB);
        }
        else
        {
            ImGui::TextDisabled("No shadow atlas allocated");
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
    }

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

    void RenderConfigWindow::drawCullingSection()
    {
        if (ImGui::CollapsingHeader("GPU Culling", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(10.0f);

            auto& dispatcher = events::EventDispatcher::instance();

            ImGui::Text("Object-Level Culling");
            ImGui::Spacing();

            if (ImGui::Checkbox("Frustum Culling", &settings.culling.frustumCullingEnabled))
            {
                isDirty = true;
                events::render::SetFrustumCullingCommand cmd;
                cmd.enabled = settings.culling.frustumCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull objects outside camera frustum using AABB test.");
            }

            if (ImGui::Checkbox("Occlusion Culling (Hi-Z)", &settings.culling.occlusionCullingEnabled))
            {
                isDirty = true;
                events::render::SetOcclusionCullingCommand cmd;
                cmd.enabled = settings.culling.occlusionCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull objects hidden behind other geometry using Hi-Z buffer.");
            }

            if (ImGui::Checkbox("LOD Selection", &settings.culling.lodSelectionEnabled))
            {
                isDirty = true;
                events::render::SetLODSelectionCommand cmd;
                cmd.enabled = settings.culling.lodSelectionEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Automatically select LOD level based on screen size.");
            }

            if (settings.culling.lodSelectionEnabled)
            {
                if (ImGui::DragFloat("Global LOD Bias", &settings.culling.globalLodBias, 0.1f, -4.0f, 4.0f, "%.1f"))
                {
                    isDirty = true;
                    events::render::SetGlobalLodBiasCommand cmd;
                    cmd.bias = settings.culling.globalLodBias;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Global bias for LOD distance transitions.\n"
                                      "Positive = lower detail sooner (better performance)\n"
                                      "Negative = higher detail longer (better quality)\n"
                                      "0 = default behavior");
                }
            }

            ImGui::Separator();
            ImGui::Text("Meshlet-Level Culling");
            ImGui::Spacing();

            if (ImGui::Checkbox("Meshlet Frustum Culling", &settings.culling.meshletFrustumCullingEnabled))
            {
                isDirty = true;
                events::render::SetMeshletFrustumCullingCommand cmd;
                cmd.enabled = settings.culling.meshletFrustumCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull individual meshlet clusters outside camera frustum.");
            }

            if (ImGui::Checkbox("Meshlet Backface Culling", &settings.culling.meshletBackfaceCullingEnabled))
            {
                isDirty = true;
                events::render::SetMeshletBackfaceCullingCommand cmd;
                cmd.enabled = settings.culling.meshletBackfaceCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull meshlet clusters facing away from camera using cone culling.");
            }

            ImGui::Separator();
            ImGui::Text("Terrain Culling");
            ImGui::Spacing();

            if (ImGui::Checkbox("Terrain Frustum Culling", &settings.culling.terrainFrustumCullingEnabled))
            {
                isDirty = true;
                events::render::SetTerrainFrustumCullingCommand cmd;
                cmd.enabled = settings.culling.terrainFrustumCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull terrain tiles outside camera frustum.");
            }

            if (ImGui::Checkbox("Terrain Meshlet Culling", &settings.culling.terrainMeshletCullingEnabled))
            {
                isDirty = true;
                events::render::SetTerrainMeshletCullingCommand cmd;
                cmd.enabled = settings.culling.terrainMeshletCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull individual terrain meshlets for finer-grained culling.");
            }

            ImGui::Separator();
            ImGui::Text("Distance Culling");
            ImGui::Spacing();

            if (ImGui::Checkbox("Enable Distance Culling", &settings.distanceCulling.enabled))
            {
                isDirty = true;
                events::render::SetDistanceCullingCommand cmd;
                cmd.enabled = settings.distanceCulling.enabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Cull objects beyond a maximum draw distance before frustum and occlusion tests.");
            }

            if (settings.distanceCulling.enabled)
            {
                auto dispatchDistance = [&](uint32_t category, float distance) {
                    events::render::SetDrawDistanceCommand cmd;
                    cmd.category = category;
                    cmd.distance = distance;
                    dispatcher.execute(cmd);
                };

                if (ImGui::DragFloat("Static Mesh Distance", &settings.distanceCulling.staticMeshDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(0, settings.distanceCulling.staticMeshDistance);
                }
                if (ImGui::DragFloat("Terrain Distance", &settings.distanceCulling.terrainDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(1, settings.distanceCulling.terrainDistance);
                }
                if (ImGui::DragFloat("Foliage Distance", &settings.distanceCulling.foliageDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(2, settings.distanceCulling.foliageDistance);
                }
                if (ImGui::DragFloat("VFX Distance", &settings.distanceCulling.vfxDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(3, settings.distanceCulling.vfxDistance);
                }
                if (ImGui::DragFloat("Decals Distance", &settings.distanceCulling.decalDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(4, settings.distanceCulling.decalDistance);
                }
                if (ImGui::DragFloat("Billboard Distance", &settings.distanceCulling.billboardDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(5, settings.distanceCulling.billboardDistance);
                }
                if (ImGui::DragFloat("Water Distance", &settings.distanceCulling.waterDistance,
                                     10.0f, 50.0f, 50000.0f, "%.0f"))
                {
                    isDirty = true;
                    dispatchDistance(6, settings.distanceCulling.waterDistance);
                }
                if (ImGui::DragFloat("Shadow Distance Multiplier", &settings.distanceCulling.shadowDistanceMultiplier,
                                     0.05f, 0.1f, 2.0f, "%.2f"))
                {
                    isDirty = true;
                    events::render::SetShadowDistanceMultiplierCommand cmd;
                    cmd.multiplier = settings.distanceCulling.shadowDistanceMultiplier;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Multiplier for shadow pass draw distances.\nLower values = shadows disappear closer.");
                }
            }

            ImGui::Separator();
            ImGui::Text("Transparency");
            ImGui::Spacing();

            if (ImGui::Checkbox("Weighted Blended OIT", &settings.transparency.wboitEnabled))
            {
                isDirty = true;
                events::render::SetWBOITCommand cmd;
                cmd.enabled = settings.transparency.wboitEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Use Weighted Blended Order-Independent Transparency for transparent objects.\nWhen disabled, falls back to simple alpha blending.");
            }

            ImGui::Unindent(10.0f);
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
                {
                    ImGui::SetTooltip("Controls terrain LOD selection.\nHigher = more detail, Lower = less detail.");
                }

                if (ImGui::DragFloat("Error Threshold", &settings.terrain.errorThreshold, 0.1f, 0.5f, 10.0f, "%.1f"))
                {
                    isDirty = true;
                    events::render::SetTerrainErrorThresholdCommand cmd;
                    cmd.threshold = settings.terrain.errorThreshold;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Maximum allowed geometric error in pixels.\nLower = higher quality, Higher = better performance.");
                }

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
                {
                    ImGui::SetTooltip("UV scale for terrain textures.\nLower = larger texture tiles.");
                }

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

            // Enforce ordering
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

            // Enforce ordering
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

            // Enforce interval ordering
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

    void RenderConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Render Configuration", &visible))
        {
            drawCullingSection();
            drawTerrainSection();
            drawShadowSection();
            drawVFXLODSection();
            drawAnimationLODSection();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Save to Scene", ImVec2(100, 0)))
            {
                saveToScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(80, 0)))
            {
                loadFromScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(80, 0)))
            {
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
            {
                resetToDefaults();
            }

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Render settings are saved with the scene file.");
        }
        ImGui::End();
    }
}
