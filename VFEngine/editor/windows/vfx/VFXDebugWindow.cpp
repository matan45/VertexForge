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

        ImGui::SetNextWindowSize(ImVec2(400, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VFX Debug Stats", &visible))
        {
            ImGui::Text("Emitters: %u / %u", stats.activeEmitters, stats.maxEmitters);
            if (stats.maxEmitters > 0)
            {
                float emitterRatio = static_cast<float>(stats.activeEmitters) / static_cast<float>(stats.maxEmitters);
                ImGui::ProgressBar(emitterRatio, ImVec2(-1, 0), "");
            }

            ImGui::Spacing();

            ImGui::Text("Particles: %u / %u", stats.allocatedParticles, stats.maxParticles);
            if (stats.maxParticles > 0)
            {
                float particleRatio = static_cast<float>(stats.allocatedParticles) / static_cast<float>(stats.maxParticles);
                ImGui::ProgressBar(particleRatio, ImVec2(-1, 0), "");
            }

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::Text("LOD Distribution:");
            ImGui::Text("  LOD 0 (Full):    %u", stats.lodCounts[0]);
            ImGui::Text("  LOD 1 (Half):    %u", stats.lodCounts[1]);
            ImGui::Text("  LOD 2 (Quarter): %u", stats.lodCounts[2]);
            ImGui::Text("  LOD 3 (Drain):   %u", stats.lodCounts[3]);

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::Text("Emitter Pool:");
            ImGui::Text("  Warm (available): %u", stats.poolWarmSlots);
            ImGui::Text("  In use:           %u", stats.poolUsedSlots);
            ImGui::Text("  Total:            %u", stats.poolTotalSlots);

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::Text("Buffer Fragmentation: %.1f%%", stats.fragmentationPercent);
            if (stats.fragmentationPercent > 50.0f)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "(High)");
            }
        }
        ImGui::End();
    }

    void VFXDebugWindow::refreshData()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
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
        catch (const std::exception& e)
        {
            vfLogError("VFXDebugWindow: Failed to refresh budget stats: {}", e.what());
        }
    }
}
