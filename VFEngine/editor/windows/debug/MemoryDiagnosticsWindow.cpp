#include "MemoryDiagnosticsWindow.hpp"
#include "memory/MemoryDiagnostics.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <algorithm>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>

#ifdef DEBUG
#include "memory/DebugAllocatorWrapper.hpp"
#endif

namespace
{
    constexpr float MB = 1024.0f * 1024.0f;
    constexpr float GB = 1024.0f * 1024.0f * 1024.0f;

    float toMB(uint64_t bytes) { return static_cast<float>(bytes) / MB; }

    // Frag color thresholds shared by the blocks table and the fragmentation map.
    ImVec4 fragColor(float fragPercent)
    {
        if (fragPercent > 30.0f) return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        if (fragPercent > 10.0f) return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
    }
}

namespace windows
{
    void MemoryDiagnosticsWindow::draw()
    {
        if (!visible) return;

        // Sample on a fixed cadence regardless of frame rate.
        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshTimer = 0.0f;
            sample();
        }

        ImGui::SetNextWindowSize(ImVec2(640, 620), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Memory Diagnostics", &visible))
        {
            drawBudgetBar();
            ImGui::Separator();
            drawGpuAllocationPanel();
            ImGui::Separator();
            drawGpuBlocksPanel();
            ImGui::Separator();
            drawTimeSeriesPanel();
            ImGui::Separator();
            drawFragmentationMapPanel();
            ImGui::Separator();
            drawStagingPanel();
            ImGui::Separator();
            drawPoolConfigPanel();
            ImGui::Separator();
            drawLeakDetectionPanel();
        }
        ImGui::End();
    }

    void MemoryDiagnosticsWindow::sample()
    {
        using Stats = memory::GpuAllocationStats;
        deviceUsedMB.push(toMB(Stats::deviceLocalUsedBytes.load(std::memory_order_relaxed)));
        hostUsedMB.push(toMB(Stats::hostVisibleUsedBytes.load(std::memory_order_relaxed)));
        deviceFragPct.push(static_cast<float>(Stats::deviceLocalFragPercent.load(std::memory_order_relaxed)) / 100.0f);

        snapshot = memory::GpuMemorySnapshot::read();
    }

    void MemoryDiagnosticsWindow::drawBudgetBar()
    {
        uint64_t budget = memory::GpuAllocationStats::vramBudgetBytes.load(std::memory_order_relaxed);
        uint64_t usage = memory::GpuAllocationStats::vramUsageBytes.load(std::memory_order_relaxed);

        ImGui::TextUnformatted("GPU VRAM");
        if (budget == 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(budget unavailable)");
            return;
        }

        float frac = std::clamp(static_cast<float>(usage) / static_cast<float>(budget), 0.0f, 1.0f);
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%.2f / %.2f GB (%.0f%%)",
            static_cast<float>(usage) / GB, static_cast<float>(budget) / GB, frac * 100.0f);

        ImVec4 barColor = frac > 0.9f ? ImVec4(0.9f, 0.25f, 0.25f, 1.0f)
                        : frac > 0.75f ? ImVec4(0.9f, 0.7f, 0.2f, 1.0f)
                        : ImVec4(0.3f, 0.7f, 0.9f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();
    }

    void MemoryDiagnosticsWindow::drawGpuAllocationPanel()
    {
        if (ImGui::CollapsingHeader("GPU Memory Allocations", ImGuiTreeNodeFlags_DefaultOpen))
        {
            uint64_t managedCount = memory::GpuAllocationStats::managedAllocationCount.load(std::memory_order_relaxed);
            uint64_t managedBytes = memory::GpuAllocationStats::managedAllocatedBytes.load(std::memory_order_relaxed);
            uint64_t managedPeak = memory::GpuAllocationStats::managedPeakBytes.load(std::memory_order_relaxed);

            if (ImGui::BeginTable("##GpuAllocs", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Allocated", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Managed (VulkanMemoryManager)");
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(managedCount));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", toMB(managedBytes));
                ImGui::TableNextColumn();
                ImGui::Text("%.1fMB", toMB(managedPeak));

                ImGui::EndTable();
            }
        }
    }

    void MemoryDiagnosticsWindow::drawGpuBlocksPanel()
    {
        if (!ImGui::CollapsingHeader("GPU Memory Blocks", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        using Stats = memory::GpuAllocationStats;

        if (ImGui::SmallButton("Reset Peaks"))
        {
            uint64_t dlUsed = Stats::deviceLocalUsedBytes.load(std::memory_order_relaxed);
            uint64_t ded = Stats::dedicatedAllocatedBytes.load(std::memory_order_relaxed);
            uint64_t hvUsed = Stats::hostVisibleUsedBytes.load(std::memory_order_relaxed);
            uint64_t managed = Stats::managedAllocatedBytes.load(std::memory_order_relaxed);
            Stats::resetPeaks(dlUsed + ded, hvUsed, managed);
        }

        if (ImGui::BeginTable("##GpuBlocks", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Capacity", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Frag %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            // Device-local row
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Device Local");
            ImGui::TableNextColumn();
            ImGui::Text("%u", Stats::deviceLocalBlockCount.load(std::memory_order_relaxed));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::deviceLocalUsedBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::deviceLocalCapacityBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::deviceLocalPeakUsedBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            float dlFrag = static_cast<float>(Stats::deviceLocalFragPercent.load(std::memory_order_relaxed)) / 100.0f;
            ImGui::TextColored(fragColor(dlFrag), "%.1f%%", dlFrag);

            // Host-visible row
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Host Visible");
            ImGui::TableNextColumn();
            ImGui::Text("%u", Stats::hostVisibleBlockCount.load(std::memory_order_relaxed));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::hostVisibleUsedBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::hostVisibleCapacityBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::hostVisiblePeakUsedBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            float hvFrag = static_cast<float>(Stats::hostVisibleFragPercent.load(std::memory_order_relaxed)) / 100.0f;
            ImGui::TextColored(fragColor(hvFrag), "%.1f%%", hvFrag);

            // Dedicated allocations row
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(50, 50, 80, 255));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Dedicated");
            ImGui::TableNextColumn();
            ImGui::Text("%u", Stats::dedicatedAllocationCount.load(std::memory_order_relaxed));
            ImGui::TableNextColumn();
            ImGui::Text("%.1fMB", toMB(Stats::dedicatedAllocatedBytes.load(std::memory_order_relaxed)));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("N/A");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("N/A");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("N/A");

            ImGui::EndTable();
        }
    }

    void MemoryDiagnosticsWindow::drawTimeSeriesPanel()
    {
        if (!ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        // Auto-scale to the largest sample so leaks/growth stay visible.
        auto maxOf = [](const SampleRing& r) {
            float m = 0.0f;
            for (float v : r.values) m = std::max(m, v);
            return m;
        };

        float dlMax = std::max(1.0f, maxOf(deviceUsedMB) * 1.1f);
        ImGui::PlotLines("Device Used (MB)", deviceUsedMB.values, SAMPLE_COUNT, deviceUsedMB.head,
            nullptr, 0.0f, dlMax, ImVec2(-1, 60));

        float hvMax = std::max(1.0f, maxOf(hostUsedMB) * 1.1f);
        ImGui::PlotLines("Host Used (MB)", hostUsedMB.values, SAMPLE_COUNT, hostUsedMB.head,
            nullptr, 0.0f, hvMax, ImVec2(-1, 60));

        ImGui::PlotLines("Device Frag (%)", deviceFragPct.values, SAMPLE_COUNT, deviceFragPct.head,
            nullptr, 0.0f, 100.0f, ImVec2(-1, 60));
    }

    void MemoryDiagnosticsWindow::drawFragmentationMapPanel()
    {
        if (!ImGui::CollapsingHeader("Block Occupancy Map"))
            return;

        if (snapshot.blocks.empty())
        {
            ImGui::TextDisabled("No blocks allocated yet.");
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float barHeight = 16.0f;
        const ImU32 usedCol = IM_COL32(70, 130, 180, 255); // steel blue = used
        const ImU32 freeCol = IM_COL32(40, 40, 45, 255);   // dark = free
        const ImU32 borderCol = IM_COL32(90, 90, 95, 255);

        int blockIndex = 0;
        for (const auto& block : snapshot.blocks)
        {
            char label[96];
            std::snprintf(label, sizeof(label), "type %u%s%s  %.0f/%.0fMB",
                block.memoryTypeIndex,
                block.hostVisible ? " HV" : " DL",
                block.deviceAddress ? " DA" : "",
                toMB(block.used), toMB(block.capacity));
            ImGui::TextColored(fragColor(block.fragmentationPercent), "%s", label);

            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float width = std::max(64.0f, ImGui::GetContentRegionAvail().x);
            ImVec2 p1 = ImVec2(p0.x + width, p0.y + barHeight);

            // Base = fully used, then punch out the free spans.
            drawList->AddRectFilled(p0, p1, usedCol);
            if (block.capacity > 0)
            {
                for (const auto& span : block.freeSpans)
                {
                    float x0 = p0.x + width * (static_cast<float>(span.offset) / static_cast<float>(block.capacity));
                    float x1 = p0.x + width * (static_cast<float>(span.offset + span.size) / static_cast<float>(block.capacity));
                    drawList->AddRectFilled(ImVec2(x0, p0.y), ImVec2(std::max(x1, x0 + 1.0f), p1.y), freeCol);
                }
            }
            drawList->AddRect(p0, p1, borderCol);

            // Invisible button over the bar for hover tooltip.
            ImGui::InvisibleButton(("##block" + std::to_string(blockIndex)).c_str(), ImVec2(width, barHeight));
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Memory type %u%s%s\nUsed %.1fMB / %.1fMB\nFree regions: %zu\nFragmentation %.1f%%",
                    block.memoryTypeIndex,
                    block.hostVisible ? " (host-visible)" : " (device-local)",
                    block.deviceAddress ? " (device-address)" : "",
                    toMB(block.used), toMB(block.capacity),
                    block.freeSpans.size(), block.fragmentationPercent);
            }
            ImGui::Spacing();
            ++blockIndex;
        }

        if (!snapshot.dedicated.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Dedicated allocations (%zu):", snapshot.dedicated.size());
            if (ImGui::BeginTable("##Dedicated", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0, std::min(120.0f, 24.0f + snapshot.dedicated.size() * 20.0f))))
            {
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Host-Visible", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Device-Addr", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                // Largest first.
                std::vector<memory::DedicatedView> sorted = snapshot.dedicated;
                std::sort(sorted.begin(), sorted.end(),
                    [](const memory::DedicatedView& a, const memory::DedicatedView& b) { return a.size > b.size; });
                for (const auto& ded : sorted)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%.2fMB", toMB(ded.size));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ded.hostVisible ? "yes" : "no");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ded.deviceAddress ? "yes" : "no");
                }
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

            ImGui::Text("Size: %.1fMB", toMB(ringSize));
            ImGui::SameLine(200);
            ImGui::Text("Used: %.1fMB (%.0f%%)", toMB(ringUsed), utilization * 100.0f);

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
        if (ImGui::CollapsingHeader("Diagnostics & Export"))
        {
            if (ImGui::Button("Export Snapshot (CSV)"))
            {
                exportSnapshotToCsv();
            }
            ImGui::SameLine();
            if (ImGui::Button("Log Full Summary"))
            {
                memory::MemoryDiagnostics::instance().logSummary();
            }
            if (!lastExportPath.empty())
            {
                ImGui::TextDisabled("Saved: %s", lastExportPath.c_str());
            }

#ifdef DEBUG
            ImGui::Separator();
            if (ImGui::Button("Report Leaks to Log"))
            {
                memory::DebugAllocatorTracker::instance().reportLeaks();
            }
#else
            ImGui::TextDisabled("Leak detection only available in Debug builds");
#endif
        }
    }

    void MemoryDiagnosticsWindow::exportSnapshotToCsv()
    {
        const char* path = "gpu_memory_snapshot.csv";
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            vfLogWarning("MemoryDiagnostics: failed to open {} for export", path);
            return;
        }

        using Stats = memory::GpuAllocationStats;
        out << "section,key,value\n";
        out << "vram,budget_bytes," << Stats::vramBudgetBytes.load(std::memory_order_relaxed) << "\n";
        out << "vram,usage_bytes," << Stats::vramUsageBytes.load(std::memory_order_relaxed) << "\n";
        out << "managed,count," << Stats::managedAllocationCount.load(std::memory_order_relaxed) << "\n";
        out << "managed,bytes," << Stats::managedAllocatedBytes.load(std::memory_order_relaxed) << "\n";
        out << "managed,peak_bytes," << Stats::managedPeakBytes.load(std::memory_order_relaxed) << "\n";
        out << "dedicated,count," << Stats::dedicatedAllocationCount.load(std::memory_order_relaxed) << "\n";
        out << "dedicated,bytes," << Stats::dedicatedAllocatedBytes.load(std::memory_order_relaxed) << "\n";

        out << "\nblock_index,memory_type,host_visible,device_address,used_bytes,capacity_bytes,free_regions,frag_percent\n";
        int i = 0;
        for (const auto& b : snapshot.blocks)
        {
            out << i++ << ',' << b.memoryTypeIndex << ',' << (b.hostVisible ? 1 : 0) << ','
                << (b.deviceAddress ? 1 : 0) << ',' << b.used << ',' << b.capacity << ','
                << b.freeSpans.size() << ',' << b.fragmentationPercent << "\n";
        }

        out.close();
        lastExportPath = path;
        vfLogInfo("MemoryDiagnostics: exported snapshot to {}", path);
    }
}
