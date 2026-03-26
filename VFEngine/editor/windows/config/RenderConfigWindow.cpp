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

        if (!std::isfinite(settings.terrain.lodBias) || settings.terrain.lodBias < 0.1f || settings.terrain.lodBias > 10.0f)
            settings.terrain.lodBias = 1.0f;
        if (!std::isfinite(settings.terrain.errorThreshold) || settings.terrain.errorThreshold < 0.1f || settings.terrain.errorThreshold > 20.0f)
            settings.terrain.errorThreshold = 2.0f;
        if (!std::isfinite(settings.terrain.textureScale) || settings.terrain.textureScale < 0.001f || settings.terrain.textureScale > 10.0f)
            settings.terrain.textureScale = 0.1f;
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

        applyVFXLODSettings();
        applyAnimationLODSettings();
    }

    void RenderConfigWindow::applyVFXLODSettings()
    {
        services::events::vfxruntime::SetVFXLODConfigCommand cmd;
        cmd.lod0Distance = settings.vfxLOD.lod0Distance;
        cmd.lod1Distance = settings.vfxLOD.lod1Distance;
        cmd.lod2Distance = settings.vfxLOD.lod2Distance;
        cmd.transitionZone = settings.vfxLOD.transitionZone;
        events::EventDispatcher::instance().execute(cmd);
    }

    void RenderConfigWindow::applyAnimationLODSettings()
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
        events::EventDispatcher::instance().execute(cmd);
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
                ImGui::SetTooltip("Cull objects outside camera frustum using AABB test.");

            if (ImGui::Checkbox("Occlusion Culling (Hi-Z)", &settings.culling.occlusionCullingEnabled))
            {
                isDirty = true;
                events::render::SetOcclusionCullingCommand cmd;
                cmd.enabled = settings.culling.occlusionCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cull objects hidden behind other geometry using Hi-Z buffer.");

            if (ImGui::Checkbox("LOD Selection", &settings.culling.lodSelectionEnabled))
            {
                isDirty = true;
                events::render::SetLODSelectionCommand cmd;
                cmd.enabled = settings.culling.lodSelectionEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Automatically select LOD level based on screen size.");

            if (settings.culling.lodSelectionEnabled)
            {
                if (ImGui::Checkbox("LOD Crossfade", &settings.culling.lodCrossfadeEnabled))
                {
                    isDirty = true;
                    events::render::SetLODCrossfadeCommand cmd;
                    cmd.enabled = settings.culling.lodCrossfadeEnabled;
                    dispatcher.execute(cmd);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Dithered crossfade between LOD levels to reduce popping.");

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
                ImGui::SetTooltip("Cull individual meshlet clusters outside camera frustum.");

            if (ImGui::Checkbox("Meshlet Backface Culling", &settings.culling.meshletBackfaceCullingEnabled))
            {
                isDirty = true;
                events::render::SetMeshletBackfaceCullingCommand cmd;
                cmd.enabled = settings.culling.meshletBackfaceCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cull meshlet clusters facing away from camera using cone culling.");

            if (ImGui::Checkbox("Meshlet Occlusion Culling", &settings.culling.meshletOcclusionCullingEnabled))
            {
                isDirty = true;
                events::render::SetMeshletOcclusionCullingCommand cmd;
                cmd.enabled = settings.culling.meshletOcclusionCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cull meshlets hidden behind occluders using Hi-Z depth prepass.");

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
                ImGui::SetTooltip("Cull terrain tiles outside camera frustum.");

            if (ImGui::Checkbox("Terrain Meshlet Culling", &settings.culling.terrainMeshletCullingEnabled))
            {
                isDirty = true;
                events::render::SetTerrainMeshletCullingCommand cmd;
                cmd.enabled = settings.culling.terrainMeshletCullingEnabled;
                dispatcher.execute(cmd);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cull individual terrain meshlets for finer-grained culling.");

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
                ImGui::SetTooltip("Cull objects beyond a maximum draw distance before frustum and occlusion tests.");

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
                    ImGui::SetTooltip("Multiplier for shadow pass draw distances.\nLower values = shadows disappear closer.");
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
                ImGui::SetTooltip("Use Weighted Blended Order-Independent Transparency for transparent objects.\nWhen disabled, falls back to simple alpha blending.");

            ImGui::Unindent(10.0f);
        }
    }

    void RenderConfigWindow::draw()
    {
        if (!visible)
            return;

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
                saveToScene();
            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(80, 0)))
                loadFromScene();
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(80, 0)))
                applySettings();
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
                resetToDefaults();

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
