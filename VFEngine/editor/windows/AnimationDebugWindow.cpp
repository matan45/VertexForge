#include "AnimationDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/animation/AnimationBudgetEvents.hpp"
#include "imgui.h"
#include "print/Log.hpp"
#include <stdexcept>

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

        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
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
        }
        ImGui::End();
    }

    void AnimationDebugWindow::refreshData()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            services::events::animation::GetAnimationBudgetStatsQuery query;
            auto result = dispatcher.query(query);

            stats.totalAnimators = result.totalAnimators;
            stats.culledEntities = result.culledEntities;
            for (int i = 0; i < 4; ++i)
                stats.lodCounts[i] = result.lodCounts[i];
            stats.pendingStreamingInits = result.pendingStreamingInits;
        }
        catch (const std::exception& e)
        {
            vfLogError("AnimationDebugWindow: Failed to refresh budget stats: {}", e.what());
        }
    }
}
