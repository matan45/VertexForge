#include "RenderConfigWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/SceneEvents.hpp"
#include "events/RenderEvents.hpp"
#include <imgui.h>
#include <algorithm>

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

            ImGui::Separator();
            ImGui::Text("Statistics");
            ImGui::Spacing();

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
            drawShadowSection();

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
