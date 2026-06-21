#include "RTTDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderTextureEvents.hpp"
#include <imgui.h>
#include <vector>

namespace windows
{
    void RTTDebugWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("RTT Debug", &visible))
        {
            std::vector<services::RenderTextureDebugInfo> infos;
            try
            {
                infos = events::EventDispatcher::instance().query(
                    services::events::rendertexture::GetActiveRenderTexturesQuery{});
            }
            catch (const std::exception&)
            {
                // Query handler not yet registered
            }

            // Advisory EveryFrame budget: each EveryFrame RTT re-renders the
            // whole scene, so flag when too many are active at once.
            static constexpr int kEveryFrameSoftBudget = 3;
            int everyFrameCount = 0;
            for (const auto& info : infos)
            {
                if (info.enabled && info.updateMode == 0)
                    ++everyFrameCount;
            }

            if (everyFrameCount > kEveryFrameSoftBudget)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                    "%d EveryFrame RTTs active (budget %d) - consider OnDemand/FixedInterval; each re-renders the whole scene.",
                    everyFrameCount, kEveryFrameSoftBudget);
            }

            if (infos.empty())
            {
                ImGui::TextDisabled("No active render textures (enter Play mode).");
            }
            else if (ImGui::BeginTable("##rtt", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Order");
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Resolution");
                ImGui::TableSetupColumn("Update Mode");
                ImGui::TableSetupColumn("Priority");
                ImGui::TableSetupColumn("State");
                ImGui::TableHeadersRow();

                // Vector is already in render order.
                for (size_t i = 0; i < infos.size(); ++i)
                {
                    const auto& info = infos[i];

                    const char* updateModeText;
                    switch (info.updateMode)
                    {
                    case 0:  updateModeText = "Every Frame"; break;
                    case 1:  updateModeText = "On Demand"; break;
                    case 2:  updateModeText = "Fixed Interval"; break;
                    default: updateModeText = "Unknown"; break;
                    }

                    ImGui::TableNextRow();

                    if (!info.enabled)
                    {
                        // Whole row greyed for disabled controllers.
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%zu", i);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%u", info.textureId);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%ux%u", info.width, info.height);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", updateModeText);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%u", info.priority);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("disabled");
                    }
                    else
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("%zu", i);
                        ImGui::TableNextColumn();
                        ImGui::Text("%u", info.textureId);
                        ImGui::TableNextColumn();
                        ImGui::Text("%ux%u", info.width, info.height);
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", updateModeText);
                        ImGui::TableNextColumn();
                        ImGui::Text("%u", info.priority);
                        ImGui::TableNextColumn();
                        if (info.submittedLastFrame)
                            ImGui::Text("rendered");
                        else if (info.hasRendered)
                            ImGui::Text("idle");
                        else
                            ImGui::Text("pending");
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}
