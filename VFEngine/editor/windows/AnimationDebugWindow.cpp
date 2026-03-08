#include "AnimationDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationBudgetEvents.hpp"
#include "imgui.h"

namespace windows
{
    void AnimationDebugWindow::draw()
    {
        if (!visible) return;

        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshData();
            refreshTimer = 0.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Animation Debug Stats", &visible))
        {
            // Active animators
            uint32_t activeEvaluated = stats.totalAnimators - stats.culledEntities;
            ImGui::Text("Total Animators: %u", stats.totalAnimators);
            ImGui::Text("Evaluated This Frame: %u", activeEvaluated);
            ImGui::Text("Frustum Culled: %u", stats.culledEntities);

            ImGui::Spacing();
            ImGui::Separator();

            // LOD distribution
            ImGui::Text("LOD Distribution:");
            ImGui::Text("  LOD 0 (Full):       %u", stats.lodCounts[0]);
            ImGui::Text("  LOD 1 (Half Rate):  %u", stats.lodCounts[1]);
            ImGui::Text("  LOD 2 (Low Rate):   %u", stats.lodCounts[2]);
            ImGui::Text("  LOD 3 (Frozen):     %u", stats.lodCounts[3]);

            ImGui::Spacing();
            ImGui::Separator();

            // Streaming
            ImGui::Text("Streaming:");
            ImGui::Text("  Pending Init: %u", stats.pendingStreamingInits);

            ImGui::Spacing();
            ImGui::Separator();

            // Configuration section (ST-10)
            drawConfigSection();
        }
        ImGui::End();
    }

    void AnimationDebugWindow::drawConfigSection()
    {
        if (ImGui::CollapsingHeader("LOD Configuration"))
        {
            bool configChanged = false;

            ImGui::Text("Distance Thresholds (meters):");
            configChanged |= ImGui::SliderFloat("LOD 0 Max##anim", &lodDistances[0], 5.0f, 50.0f, "%.0f m");
            configChanged |= ImGui::SliderFloat("LOD 1 Max##anim", &lodDistances[1], 25.0f, 150.0f, "%.0f m");
            configChanged |= ImGui::SliderFloat("LOD 2 Max##anim", &lodDistances[2], 50.0f, 300.0f, "%.0f m");
            configChanged |= ImGui::SliderFloat("LOD 3 Max##anim", &lodDistances[3], 100.0f, 500.0f, "%.0f m");

            ImGui::Spacing();
            ImGui::Text("Update Intervals (frames):");
            configChanged |= ImGui::SliderInt("LOD 0 Interval##anim", &lodUpdateIntervals[0], 1, 1, "%d");
            configChanged |= ImGui::SliderInt("LOD 1 Interval##anim", &lodUpdateIntervals[1], 1, 4, "%d");
            configChanged |= ImGui::SliderInt("LOD 2 Interval##anim", &lodUpdateIntervals[2], 2, 16, "%d");

            ImGui::Spacing();
            configChanged |= ImGui::SliderInt("Max Streaming Init/Frame", &maxStreamingInitPerFrame, 1, 16);

            if (configChanged)
            {
                // Apply config changes via events would go here
                // For now, config is display-only; runtime integration in future
            }
        }
    }

    void AnimationDebugWindow::refreshData()
    {
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            services::events::animation::GetAnimationBudgetStatsQuery query;
            auto result = dispatcher.query(query);

            stats.totalAnimators = result.totalAnimators;
            stats.culledEntities = result.culledEntities;
            for (int i = 0; i < 4; ++i)
                stats.lodCounts[i] = result.lodCounts[i];
            stats.pendingStreamingInits = result.pendingStreamingInits;
        }
        catch (...)
        {
            // Query handler may not be registered yet
        }
    }
}
