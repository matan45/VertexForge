#include "MemoryDiagnosticsWindow.hpp"
#include "memory/MemoryDiagnostics.hpp"
#include "memory/GpuAllocationStats.hpp"
#include <imgui.h>
#include <algorithm>

#ifdef DEBUG
#include "memory/DebugAllocatorWrapper.hpp"
#endif

namespace windows
{
    void MemoryDiagnosticsWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(620, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Memory Diagnostics", &visible))
        {
            drawGpuAllocationPanel();
            ImGui::Separator();
            drawGpuBlocksPanel();
            ImGui::Separator();
            drawStagingPanel();
            ImGui::Separator();
            drawPoolConfigPanel();
            ImGui::Separator();
            drawLeakDetectionPanel();
        }
        ImGui::End();
    }

    void MemoryDiagnosticsWindow::drawGpuAllocationPanel()
    {
        if (ImGui::CollapsingHeader("GPU Memory Allocations", ImGuiTreeNodeFlags_DefaultOpen))
        {
            uint64_t managedCount = memory::GpuAllocationStats::managedAllocationCount.load(std::memory_order_relaxed);
            uint64_t managedBytes = memory::GpuAllocationStats::managedAllocatedBytes.load(std::memory_order_relaxed);

            if (ImGui::BeginTable("##GpuAllocs", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Allocated", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Managed (VulkanMemoryManager)");
                ImGui::TableNextColumn();
                ImGui::Text("%llu", managedCount);
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(managedBytes) / (1024.0f * 1024.0f));

                ImGui::EndTable();
            }
        }
    }

    void MemoryDiagnosticsWindow::drawGpuBlocksPanel()
    {
        if (ImGui::CollapsingHeader("GPU Memory Blocks", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##GpuBlocks", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Capacity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Frag %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                // Device-local row
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Device Local");
                ImGui::TableNextColumn();
                ImGui::Text("%u", memory::GpuAllocationStats::deviceLocalBlockCount.load(std::memory_order_relaxed));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(memory::GpuAllocationStats::deviceLocalUsedBytes.load(std::memory_order_relaxed)) / (1024.0f * 1024.0f));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(memory::GpuAllocationStats::deviceLocalCapacityBytes.load(std::memory_order_relaxed)) / (1024.0f * 1024.0f));
                ImGui::TableNextColumn();
                float dlFrag = static_cast<float>(memory::GpuAllocationStats::deviceLocalFragPercent.load(std::memory_order_relaxed)) / 100.0f;
                if (dlFrag > 30.0f)
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.1f%%", dlFrag);
                else if (dlFrag > 10.0f)
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.1f%%", dlFrag);
                else
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%.1f%%", dlFrag);

                // Host-visible row
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Host Visible");
                ImGui::TableNextColumn();
                ImGui::Text("%u", memory::GpuAllocationStats::hostVisibleBlockCount.load(std::memory_order_relaxed));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(memory::GpuAllocationStats::hostVisibleUsedBytes.load(std::memory_order_relaxed)) / (1024.0f * 1024.0f));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(memory::GpuAllocationStats::hostVisibleCapacityBytes.load(std::memory_order_relaxed)) / (1024.0f * 1024.0f));
                ImGui::TableNextColumn();
                float hvFrag = static_cast<float>(memory::GpuAllocationStats::hostVisibleFragPercent.load(std::memory_order_relaxed)) / 100.0f;
                if (hvFrag > 30.0f)
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.1f%%", hvFrag);
                else if (hvFrag > 10.0f)
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%.1f%%", hvFrag);
                else
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%.1f%%", hvFrag);

                // Dedicated allocations row
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 50, 80, 255));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Dedicated");
                ImGui::TableNextColumn();
                ImGui::Text("%u", memory::GpuAllocationStats::dedicatedAllocationCount.load(std::memory_order_relaxed));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", static_cast<float>(memory::GpuAllocationStats::dedicatedAllocatedBytes.load(std::memory_order_relaxed)) / (1024.0f * 1024.0f));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("N/A");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("N/A");

                ImGui::EndTable();
            }
        }
    }

    void MemoryDiagnosticsWindow::drawStagingPanel()
    {
        if (ImGui::CollapsingHeader("Staging Ring Buffer"))
        {
            uint64_t ringSize = memory::GpuAllocationStats::stagingRingSize.load(std::memory_order_relaxed);
            uint64_t ringUsed = memory::GpuAllocationStats::stagingRingUsed.load(std::memory_order_relaxed);
            uint32_t pending = memory::GpuAllocationStats::stagingPendingTransfers.load(std::memory_order_relaxed);
            uint32_t overflows = memory::GpuAllocationStats::stagingOverflowCount.load(std::memory_order_relaxed);

            float utilization = ringSize > 0 ? static_cast<float>(ringUsed) / static_cast<float>(ringSize) : 0.0f;

            ImGui::Text("Size: %.1fMB", static_cast<float>(ringSize) / (1024.0f * 1024.0f));
            ImGui::SameLine(200);
            ImGui::Text("Used: %.1fMB (%.0f%%)", static_cast<float>(ringUsed) / (1024.0f * 1024.0f), utilization * 100.0f);

            ImGui::ProgressBar(utilization, ImVec2(-1, 0), "");

            ImGui::Text("Pending Transfers: %u", pending);
            ImGui::SameLine(200);
            if (overflows > 0)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Overflows: %u", overflows);
            else
                ImGui::Text("Overflows: 0");
        }
    }

    void MemoryDiagnosticsWindow::drawPoolConfigPanel()
    {
        if (ImGui::CollapsingHeader("Memory Pool Configuration"))
        {
            auto& config = memory::MemoryPoolConfig::instance();

            ImGui::TextDisabled("Changes apply on next engine restart");
            if (config.dirty) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(Modified - restart to apply)");
            }

            ImGui::Spacing();

            int deviceLocal = static_cast<int>(config.deviceLocalBlockSizeMB);
            if (ImGui::SliderInt("Device Local Block (MB)", &deviceLocal, 32, 512)) {
                config.deviceLocalBlockSizeMB = static_cast<uint64_t>(std::max(32, deviceLocal));
                config.dirty = true;
            }
            ImGui::SetItemTooltip("Size of each GPU device-local memory block. Larger = fewer blocks, less overhead. Smaller = less wasted memory.");

            int hostVisible = static_cast<int>(config.hostVisibleBlockSizeMB);
            if (ImGui::SliderInt("Host Visible Block (MB)", &hostVisible, 16, 256)) {
                config.hostVisibleBlockSizeMB = static_cast<uint64_t>(std::max(16, hostVisible));
                config.dirty = true;
            }
            ImGui::SetItemTooltip("Size of each host-visible (CPU-accessible) memory block for staging and uniform buffers.");

            int ringBuffer = static_cast<int>(config.stagingRingBufferSizeMB);
            if (ImGui::SliderInt("Staging Ring Buffer (MB)", &ringBuffer, 16, 256)) {
                config.stagingRingBufferSizeMB = static_cast<uint64_t>(std::max(16, ringBuffer));
                config.dirty = true;
            }
            ImGui::SetItemTooltip("Size of the ring buffer used for async CPU->GPU transfers. Larger = more concurrent transfers before overflow.");

            if (ImGui::Button("Reset to Defaults")) {
                config.deviceLocalBlockSizeMB = 256;
                config.hostVisibleBlockSizeMB = 64;
                config.stagingRingBufferSizeMB = 64;
                config.dirty = true;
            }
        }
    }

    void MemoryDiagnosticsWindow::drawLeakDetectionPanel()
    {
#ifdef DEBUG
        if (ImGui::CollapsingHeader("Leak Detection (Debug)"))
        {
            if (ImGui::Button("Report Leaks to Log"))
            {
                memory::DebugAllocatorTracker::instance().reportLeaks();
            }

            ImGui::SameLine();
            if (ImGui::Button("Log Full Summary"))
            {
                memory::MemoryDiagnostics::instance().logSummary();
            }
        }
#else
        ImGui::TextDisabled("Leak detection only available in Debug builds");
#endif
    }
}
