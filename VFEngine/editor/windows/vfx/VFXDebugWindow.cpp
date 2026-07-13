#include "VFXDebugWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/vfx/VFXSequenceRuntimeEvents.hpp"
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

        ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VFX Debug Stats", &visible))
        {
            if (ImGui::BeginTabBar("VFXDebugTabs"))
            {
                if (ImGui::BeginTabItem("Budget"))
                {
                    drawBudgetTab();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Combos"))
                {
                    drawCombosTab();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Instances"))
                {
                    drawInstancesTab();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Warnings"))
                {
                    drawWarningsTab();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void VFXDebugWindow::drawBudgetTab()
    {
        ImGui::Text("Emitters: %u / %u", budget.activeEmitters, budget.maxEmitters);
        if (budget.maxEmitters > 0)
        {
            float ratio = static_cast<float>(budget.activeEmitters) / static_cast<float>(budget.maxEmitters);
            ImGui::ProgressBar(ratio, ImVec2(-1, 0), "");
        }

        ImGui::Spacing();
        ImGui::Text("Particles: %u / %u", budget.allocatedParticles, budget.maxParticles);
        if (budget.maxParticles > 0)
        {
            float ratio = static_cast<float>(budget.allocatedParticles) / static_cast<float>(budget.maxParticles);
            ImGui::ProgressBar(ratio, ImVec2(-1, 0), "");
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (budget.eventBudget > 0)
        {
            ImGui::Text("Events: %u / %u", budget.eventsThisFrame, budget.eventBudget);
            if (budget.eventsDropped)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "(dropped: raw %u)",
                                   budget.rawEventsThisFrame);
            }
            else if (budget.rawEventsThisFrame == budget.eventBudget)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(full)");
            }
        }

        if (budget.channelRequestBudget > 0)
        {
            ImGui::Text("Spawn requests: %u / %u (raw %u)",
                        budget.channelAcceptedRequests,
                        budget.channelRequestBudget,
                        budget.channelRawRequests);
            if (budget.channelRingDroppedRequests > 0 || budget.channelParticleDroppedRequests > 0)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
                    "(dropped: ring %u, particles %u)",
                    budget.channelRingDroppedRequests,
                    budget.channelParticleDroppedRequests);
            }
            ImGui::Text("Channel listeners: %u", budget.channelListeners);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("LOD Distribution:");
        ImGui::Text("  LOD 0 (Full):    %u", budget.lodCounts[0]);
        ImGui::Text("  LOD 1 (Half):    %u", budget.lodCounts[1]);
        ImGui::Text("  LOD 2 (Quarter): %u", budget.lodCounts[2]);
        ImGui::Text("  LOD 3 (Drain):   %u", budget.lodCounts[3]);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Culling:");
        ImGui::Text("  Culled this frame:    %u", budget.culledEmitters);
        ImGui::Text("  Throttled this frame: %u", budget.throttledEmitters);
        if (budget.vfxCullDistance > 0.0f)
            ImGui::Text("  Cull distance:        %.1f", budget.vfxCullDistance);
        else
            ImGui::TextDisabled("  Cull distance:        (unlimited)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Emitter Pool:");
        ImGui::Text("  Warm (available): %u", budget.poolWarmSlots);
        ImGui::Text("  In use:           %u", budget.poolUsedSlots);
        ImGui::Text("  Total:            %u", budget.poolTotalSlots);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Buffer Fragmentation: %.1f%%", budget.fragmentationPercent);
        if (budget.fragmentationPercent > 50.0f)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "(High)");
        }
    }

    void VFXDebugWindow::drawCombosTab()
    {
        ImGui::Text("Active combos:        %u", combos.activeCombos);
        ImGui::Text("Playing combos:       %u", combos.playingCombos);
        ImGui::Text("Live child instances: %u", combos.liveChildInstances);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Pre-spawn culled:     %u", combos.culledSpawns);
        ImGui::Text("Pooled reuses:        %u", combos.pooledReuses);
    }

    void VFXDebugWindow::drawInstancesTab()
    {
        ImGui::Text("Active instances: %zu", instances.size());
        ImGui::Spacing();

        if (ImGui::BeginTable("VFXInstances", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("World Pos");
            ImGui::TableSetupColumn("Extents");
            ImGui::TableSetupColumn("Vis");
            ImGui::TableSetupColumn("LOD");
            ImGui::TableSetupColumn("Particles");
            ImGui::TableHeadersRow();

            for (const auto& e : instances)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.id);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f, %.1f, %.1f", e.worldPos[0], e.worldPos[1], e.worldPos[2]);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f, %.1f, %.1f", e.extents[0], e.extents[1], e.extents[2]);
                ImGui::TableNextColumn();
                if (e.inFrustum)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "vis");
                else
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "cull");
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.lod);
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.particleCount);
            }
            ImGui::EndTable();
        }
    }

    void VFXDebugWindow::drawWarningsTab()
    {
        if (warnings.empty())
        {
            ImGui::TextDisabled("No runtime warnings.");
            return;
        }

        ImGui::Text("Recent warnings: %zu", warnings.size());
        ImGui::Spacing();

        if (ImGui::BeginTable("VFXWarnings", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Message");
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableHeadersRow();

            for (const auto& w : warnings)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(w.source.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", w.message.c_str());
                ImGui::TableNextColumn();
                if (w.count > 1)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%u", w.count);
                else
                    ImGui::Text("%u", w.count);
            }
            ImGui::EndTable();
        }
    }

    void VFXDebugWindow::refreshData()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Each query is guarded independently: an unregistered handler throws, but
        // that should only blank its own tab (e.g. no runtime service present).
        try
        {
            auto r = dispatcher.query(services::events::vfxruntime::GetVFXBudgetStatsQuery{});
            budget.activeEmitters = r.activeEmitters;
            budget.maxEmitters = r.maxEmitters;
            budget.allocatedParticles = r.allocatedParticles;
            budget.maxParticles = r.maxParticles;
            for (int i = 0; i < 4; ++i) budget.lodCounts[i] = r.lodCounts[i];
            budget.fragmentationPercent = r.fragmentationPercent;
            budget.poolWarmSlots = r.poolWarmSlots;
            budget.poolUsedSlots = r.poolUsedSlots;
            budget.poolTotalSlots = r.poolTotalSlots;
            budget.culledEmitters = r.culledEmitters;
            budget.throttledEmitters = r.throttledEmitters;
            budget.vfxCullDistance = r.vfxCullDistance;
            budget.eventsThisFrame = r.eventsThisFrame;
            budget.rawEventsThisFrame = r.rawEventsThisFrame;
            budget.eventBudget = r.eventBudget;
            budget.eventsDropped = r.eventsDropped;
            budget.channelListeners = r.channelListeners;
            budget.channelRawRequests = r.channelRawRequests;
            budget.channelAcceptedRequests = r.channelAcceptedRequests;
            budget.channelRingDroppedRequests = r.channelRingDroppedRequests;
            budget.channelParticleDroppedRequests = r.channelParticleDroppedRequests;
            budget.channelRequestBudget = r.channelRequestBudget;
        }
        catch (const std::exception&) { /* no runtime provider */ }

        try
        {
            auto r = dispatcher.query(services::events::vfxsequence::GetVFXComboStatsQuery{});
            combos.activeCombos = r.activeCombos;
            combos.playingCombos = r.playingCombos;
            combos.liveChildInstances = r.liveChildInstances;
            combos.culledSpawns = r.culledSpawns;
            combos.pooledReuses = r.pooledReuses;
        }
        catch (const std::exception&) {}

        try
        {
            auto r = dispatcher.query(services::events::vfxruntime::GetVFXInstanceDebugQuery{});
            instances.clear();
            instances.reserve(r.instances.size());
            for (const auto& e : r.instances)
            {
                InstanceEntry ie;
                ie.id = e.id;
                ie.worldPos[0] = e.worldPosition.x;
                ie.worldPos[1] = e.worldPosition.y;
                ie.worldPos[2] = e.worldPosition.z;
                ie.extents[0] = e.extents.x;
                ie.extents[1] = e.extents.y;
                ie.extents[2] = e.extents.z;
                ie.inFrustum = e.inFrustum;
                ie.lod = e.lod;
                ie.particleCount = e.particleCount;
                ie.priority = e.priority;
                instances.push_back(ie);
            }
        }
        catch (const std::exception&) {}

        try
        {
            auto r = dispatcher.query(services::events::vfxruntime::GetVFXRecentWarningsQuery{});
            warnings.clear();
            warnings.reserve(r.warnings.size());
            for (const auto& w : r.warnings)
                warnings.push_back(WarningEntry{w.source, w.message, w.count, w.lastSeq});
        }
        catch (const std::exception&) {}
    }
}
