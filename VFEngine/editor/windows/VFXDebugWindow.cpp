#include "VFXDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "imgui.h"
#include "print/Log.hpp"

namespace windows
{
    void VFXDebugWindow::draw()
    {
        if (!visible) return;

        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshData();
            refreshTimer = 0.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VFX Debug Stats", &visible))
        {
            // Emitter budget
            ImGui::Text("Emitters: %u / %u", stats.activeEmitters, stats.maxEmitters);
            if (stats.maxEmitters > 0)
            {
                float emitterRatio = static_cast<float>(stats.activeEmitters) / static_cast<float>(stats.maxEmitters);
                ImGui::ProgressBar(emitterRatio, ImVec2(-1, 0), "");
            }

            ImGui::Spacing();

            // Particle budget
            ImGui::Text("Particles: %u / %u", stats.allocatedParticles, stats.maxParticles);
            if (stats.maxParticles > 0)
            {
                float particleRatio = static_cast<float>(stats.allocatedParticles) / static_cast<float>(stats.maxParticles);
                ImGui::ProgressBar(particleRatio, ImVec2(-1, 0), "");
            }

            ImGui::Spacing();
            ImGui::Separator();

            // LOD breakdown
            ImGui::Text("LOD Distribution:");
            ImGui::Text("  LOD 0 (Full):    %u", stats.lodCounts[0]);
            ImGui::Text("  LOD 1 (Half):    %u", stats.lodCounts[1]);
            ImGui::Text("  LOD 2 (Quarter): %u", stats.lodCounts[2]);
            ImGui::Text("  LOD 3 (Drain):   %u", stats.lodCounts[3]);

            ImGui::Spacing();
            ImGui::Separator();

            // Pool stats
            ImGui::Text("Emitter Pool:");
            ImGui::Text("  Warm (available): %u", stats.poolWarmSlots);
            ImGui::Text("  In use:           %u", stats.poolUsedSlots);
            ImGui::Text("  Total:            %u", stats.poolTotalSlots);

            ImGui::Spacing();
            ImGui::Separator();

            // Fragmentation
            ImGui::Text("Buffer Fragmentation: %.1f%%", stats.fragmentationPercent);
            if (stats.fragmentationPercent > 50.0f)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "(High)");
            }

            ImGui::Spacing();
            ImGui::Separator();

            drawConfigSection();
        }
        ImGui::End();
    }

    void VFXDebugWindow::drawConfigSection()
    {
        if (!configLoaded)
        {
            try
            {
                auto& dispatcher = ::events::EventDispatcher::instance();
                auto config = dispatcher.query(services::events::vfxruntime::GetVFXLODConfigQuery{});
                lodDistances[0] = config.lod0Distance;
                lodDistances[1] = config.lod1Distance;
                lodDistances[2] = config.lod2Distance;
                transitionZone = config.transitionZone;
                configLoaded = true;
            }
            catch (...)
            {
                vfLogError("VFXDebugWindow: Failed to load LOD config");
                return;
            }
        }

        if (ImGui::CollapsingHeader("LOD Config"))
        {
            bool changed = false;

            changed |= ImGui::SliderFloat("LOD 0 (Full)", &lodDistances[0], 10.0f, 200.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 1 (Half)", &lodDistances[1], 20.0f, 400.0f, "%.0f m");
            changed |= ImGui::SliderFloat("LOD 2 (Quarter)", &lodDistances[2], 50.0f, 800.0f, "%.0f m");
            changed |= ImGui::SliderFloat("Transition Zone", &transitionZone, 1.0f, 50.0f, "%.0f m");

            // Enforce ordering
            if (lodDistances[1] <= lodDistances[0])
                lodDistances[1] = lodDistances[0] + 1.0f;
            if (lodDistances[2] <= lodDistances[1])
                lodDistances[2] = lodDistances[1] + 1.0f;

            if (changed)
            {
                try
                {
                    auto& dispatcher = ::events::EventDispatcher::instance();
                    services::events::vfxruntime::SetVFXLODConfigCommand cmd;
                    cmd.lod0Distance = lodDistances[0];
                    cmd.lod1Distance = lodDistances[1];
                    cmd.lod2Distance = lodDistances[2];
                    cmd.transitionZone = transitionZone;
                    dispatcher.execute(cmd);
                }
                catch (...)
                {
                    vfLogError("VFXDebugWindow: Failed to apply LOD config");
                }
            }
        }
    }

    void VFXDebugWindow::refreshData()
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            services::events::vfxruntime::GetVFXBudgetStatsQuery query;
            auto result = dispatcher.query(query);

            stats.activeEmitters = result.activeEmitters;
            stats.maxEmitters = result.maxEmitters;
            stats.allocatedParticles = result.allocatedParticles;
            stats.maxParticles = result.maxParticles;
            for (int i = 0; i < 4; ++i)
                stats.lodCounts[i] = result.lodCounts[i];
            stats.fragmentationPercent = result.fragmentationPercent;
            stats.poolWarmSlots = result.poolWarmSlots;
            stats.poolUsedSlots = result.poolUsedSlots;
            stats.poolTotalSlots = result.poolTotalSlots;
        }
        catch (...)
        {
            vfLogError("VFXDebugWindow: Failed to refresh budget stats");
        }
    }
}
