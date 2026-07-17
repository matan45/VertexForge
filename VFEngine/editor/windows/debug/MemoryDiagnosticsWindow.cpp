#include "MemoryDiagnosticsWindow.hpp"
#include "ProcessMemoryProbe.hpp"
#include "memory/MemoryDiagnostics.hpp"
#include "memory/GpuAllocationStats.hpp"
#include "cpumem/CpuMemoryManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/lifecycle/AssetLifecycleEvents.hpp"
#include "events/memory/CpuMemoryEvents.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cctype>
#include <ctime>

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

    // VK-1539 diff coloring: growth (positive delta) reads as a potential leak, shrink as freed.
    ImVec4 deltaColor(int64_t byteDelta)
    {
        if (byteDelta > 0) return ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
        if (byteDelta < 0) return ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }

    // Filesystem-safe version of a capture label, for export filenames.
    std::string sanitizeLabel(const std::string& label)
    {
        std::string s;
        s.reserve(label.size());
        for (char c : label)
            s.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_');
        if (s.empty()) s = "capture";
        return s;
    }

    // Render one diff axis (CPU categories / GPU heaps / per-asset) as a colored delta table.
    // Unchanged rows are hidden so the leak hunt focuses on what actually moved.
    template <class Row>
    void drawDeltaTable(const char* id, const char* title, const std::vector<memory::DiffRow<Row>>& deltas)
    {
        if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
            return;

        int shown = 0;
        for (const auto& d : deltas)
            if (d.kind != memory::DeltaKind::Unchanged) ++shown;
        if (shown == 0)
        {
            ImGui::TextDisabled("No changes.");
            return;
        }

        if (ImGui::BeginTable(id, 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, std::min(200.0f, 24.0f + shown * 20.0f))))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Change",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Delta MB", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            for (const auto& d : deltas)
            {
                if (d.kind == memory::DeltaKind::Unchanged) continue;
                const ImVec4 col = deltaColor(d.byteDelta);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(d.key.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(col, "%s", memory::deltaKindName(d.kind));
                ImGui::TableNextColumn();
                ImGui::TextColored(col, "%+.2f", static_cast<double>(d.byteDelta) / MB);
            }
            ImGui::EndTable();
        }
    }
}

namespace windows
{
    void MemoryDiagnosticsWindow::draw()
    {
        // Publish visibility every frame so the renderer only refreshes diagnostics while
        // this window is open. draw() is called each frame even when hidden, so this also
        // clears the flag the frame the window closes.
        memory::GpuAllocationStats::diagnosticsActive.store(visible, std::memory_order_relaxed);

        if (!visible) return;

        // Sample on a fixed cadence regardless of frame rate.
        refreshTimer += ImGui::GetIO().DeltaTime;
        if (refreshTimer >= REFRESH_INTERVAL)
        {
            refreshTimer = 0.0f;
            sample();
        }

        ImGui::SetNextWindowSize(ImVec2(640, 680), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Memory Diagnostics", &visible))
        {
            if (ImGui::BeginTabBar("##MemDiagTabs"))
            {
                if (ImGui::BeginTabItem("GPU"))
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
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("CPU"))
                {
                    drawCpuTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("VRAM Assets"))
                {
                    drawVramTab();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Snapshots"))
                {
                    drawSnapshotsTab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
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

        // CPU sampling — cold path snapshot from CpuMemoryManager.
        cpuSnapshot = memory::CpuMemoryManager::instance().snapshot();
        cpuTrackedMB.push(toMB(cpuSnapshot.totalTrackedBytes));

        // VK-1539 per-asset VRAM attribution, published by the render thread (GPUDrivenRenderer).
        vramSnapshot = memory::VramAssetSnapshot::read();
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
            uint64_t hvUsed = Stats::hostVisibleUsedBytes.load(std::memory_order_relaxed);
            uint64_t managed = Stats::managedAllocatedBytes.load(std::memory_order_relaxed);
            Stats::resetPeaks(dlUsed, hvUsed, managed);
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
        auto last = [](const SampleRing& r) {
            return r.values[(r.head - 1 + SAMPLE_COUNT) % SAMPLE_COUNT];
        };

        // Label above + hidden PlotLines label (its side label would otherwise be
        // clipped off the right edge of a full-width plot); current value as overlay.
        auto plot = [&](const char* id, const char* title, const SampleRing& ring,
                        float scaleMax, const char* fmt) {
            char overlay[48];
            std::snprintf(overlay, sizeof(overlay), fmt, last(ring));
            ImGui::TextUnformatted(title);
            ImGui::PlotLines(id, ring.values, SAMPLE_COUNT, ring.head,
                overlay, 0.0f, scaleMax, ImVec2(-1, 60));
        };

        plot("##devUsed", "Device Used (MB)", deviceUsedMB, std::max(1.0f, maxOf(deviceUsedMB) * 1.1f), "%.1f MB");
        plot("##hostUsed", "Host Used (MB)", hostUsedMB, std::max(1.0f, maxOf(hostUsedMB) * 1.1f), "%.1f MB");
        plot("##devFrag", "Device Frag (%)", deviceFragPct, 100.0f, "%.1f %%");
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

    // -----------------------------------------------------------------------
    // CPU tab — reads directly from CpuMemoryManager (Editor links CpuMemory).
    // -----------------------------------------------------------------------

    void MemoryDiagnosticsWindow::drawCpuTab()
    {
        drawCpuBudgetBar();
        ImGui::Separator();
        drawCpuProcessMemoryPanel();
        ImGui::Separator();
        drawGateStatePanel();
        ImGui::Separator();
        drawCpuCategoryTable();
        ImGui::Separator();
        drawCpuTimeSeriesPanel();
    }

    void MemoryDiagnosticsWindow::drawCpuProcessMemoryPanel()
    {
        if (!ImGui::CollapsingHeader("Process Memory (OS)", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const ProcessMemoryInfo os = queryProcessMemory();

        // Tracked = authoritative decoded assets (AssetLifecycleManager) + CpuMemory's
        // own categories (transient + staging). Reservations are pre-decode estimates,
        // not extra real RAM, so they are intentionally excluded here.
        uint64_t decodedBytes = 0;
        try
        {
            auto status = events::EventDispatcher::instance().query(
                events::lifecycle::QueryMemoryBudgetQuery{});
            decodedBytes = static_cast<uint64_t>(status.trackedBytes);
        }
        catch (const std::exception&) {}

        const uint64_t tracked   = decodedBytes + cpuSnapshot.totalTrackedBytes;
        const uint64_t untracked = os.workingSetBytes > tracked ? (os.workingSetBytes - tracked) : 0;

        if (os.workingSetBytes == 0)
        {
            ImGui::TextDisabled("Process memory query unavailable on this platform.");
            return;
        }

        ImGui::Text("Process working set (RSS): %.2f GB", static_cast<float>(os.workingSetBytes) / GB);
        ImGui::Text("  Tracked (assets + CPU categories): %.2f GB", static_cast<float>(tracked) / GB);
        ImGui::TextDisabled("  Untracked (3rd-party / small heap / frag): %.2f GB",
            static_cast<float>(untracked) / GB);
        if (os.systemTotalBytes > 0)
        {
            ImGui::TextDisabled("System RAM: %.1f GB available / %.1f GB total",
                static_cast<float>(os.systemAvailBytes) / GB,
                static_cast<float>(os.systemTotalBytes) / GB);
        }
    }

    void MemoryDiagnosticsWindow::drawCpuBudgetBar()
    {
        // Decoded bytes come from the asset lifecycle manager (authoritative source).
        // Transient + staging are tracked by CpuMemoryManager directly.
        uint64_t decodedBytes = 0;
        try
        {
            auto status = events::EventDispatcher::instance().query(
                events::lifecycle::QueryMemoryBudgetQuery{});
            decodedBytes = static_cast<uint64_t>(status.trackedBytes);
        }
        catch (const std::exception&) {}

        const uint64_t reservedBytes = cpuSnapshot.totalReservedBytes;
        const uint64_t usedBytes     = decodedBytes + reservedBytes;
        const uint64_t budgetBytes   = cpuSnapshot.budgetBytes;

        ImGui::TextUnformatted("CPU RAM");

        if (budgetBytes == 0)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("advisory (disabled)");
            char info[128];
            std::snprintf(info, sizeof(info), "decoded %.2f GB  |  reserved %.2f MB",
                static_cast<float>(decodedBytes) / GB,
                static_cast<float>(reservedBytes) / MB);
            ImGui::TextDisabled("%s", info);
            return;
        }

        float rawFrac = budgetBytes > 0
            ? static_cast<float>(usedBytes) / static_cast<float>(budgetBytes)
            : 0.0f;
        float frac = std::clamp(rawFrac, 0.0f, 1.0f);
        // budgetBytes/usedBytes are uint64_t — subtract only when in budget so the
        // difference can't wrap to a huge value (which std::max(0.0f, …) couldn't clamp).
        float headroomGB = budgetBytes > usedBytes
            ? static_cast<float>(budgetBytes - usedBytes) / GB
            : 0.0f;

        char overlay[96];
        std::snprintf(overlay, sizeof(overlay), "%.2f / %.2f GB (%.0f%%)  headroom %.2f GB",
            static_cast<float>(usedBytes) / GB,
            static_cast<float>(budgetBytes) / GB,
            rawFrac * 100.0f,
            headroomGB);

        // Turn red when over budget.
        ImVec4 barColor = rawFrac > 1.0f ? ImVec4(0.9f, 0.25f, 0.25f, 1.0f)
                        : rawFrac > 0.85f ? ImVec4(0.9f, 0.7f, 0.2f, 1.0f)
                        : ImVec4(0.3f, 0.85f, 0.5f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();

        ImGui::TextDisabled("decoded %.2f GB  |  reserved %.2f MB",
            static_cast<float>(decodedBytes) / GB,
            static_cast<float>(reservedBytes) / MB);

        // Budget slider (in MB) — fires SetCpuMemoryBudgetCommand which sets + persists.
        ImGui::Spacing();
        static int budgetMB = 0;
        // Sync the slider to the live budget value on the first draw and whenever it
        // changes from outside (e.g. settings reload).
        int liveMB = static_cast<int>(budgetBytes / (1024 * 1024));
        if (budgetMB != liveMB) budgetMB = liveMB;

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::SliderInt("Budget (MB)", &budgetMB, 0, 65536))
        {
            events::memory::SetCpuMemoryBudgetCommand cmd;
            cmd.budgetBytes = static_cast<uint64_t>(budgetMB) * 1024 * 1024;
            try { events::EventDispatcher::instance().execute(cmd); }
            catch (const std::exception&) {}
        }
        ImGui::SetItemTooltip("0 = advisory (gate disabled). Default = 8192 MB (8 GiB).");
    }

    void MemoryDiagnosticsWindow::drawCpuCategoryTable()
    {
        if (!ImGui::CollapsingHeader("CPU Memory Categories", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        // Sort a local copy largest-first.
        std::vector<memory::CategoryView> sorted = cpuSnapshot.categories;
        std::sort(sorted.begin(), sorted.end(),
            [](const memory::CategoryView& a, const memory::CategoryView& b) { return a.bytes > b.bytes; });

        if (sorted.empty())
        {
            ImGui::TextDisabled("No categories registered yet.");
            return;
        }

        if (ImGui::BeginTable("##CpuCats", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, std::min(200.0f, 24.0f + sorted.size() * 20.0f))))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("MB",      ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Peak MB", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const auto& cat : sorted)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cat.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", memory::categoryKindName(cat.kind));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", toMB(cat.bytes));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", toMB(cat.peak));
            }

            ImGui::EndTable();
        }
    }

    void MemoryDiagnosticsWindow::drawCpuTimeSeriesPanel()
    {
        if (!ImGui::CollapsingHeader("CPU History", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        auto maxOf = [](const SampleRing& r) {
            float m = 0.0f;
            for (float v : r.values) m = std::max(m, v);
            return m;
        };
        auto last = [](const SampleRing& r) {
            return r.values[(r.head - 1 + SAMPLE_COUNT) % SAMPLE_COUNT];
        };

        char overlay[48];
        std::snprintf(overlay, sizeof(overlay), "%.1f MB", last(cpuTrackedMB));
        ImGui::TextUnformatted("CPU Tracked (MB)");
        ImGui::PlotLines("##cpuTracked", cpuTrackedMB.values, SAMPLE_COUNT, cpuTrackedMB.head,
            overlay, 0.0f, std::max(1.0f, maxOf(cpuTrackedMB) * 1.1f), ImVec2(-1, 60));
    }

    void MemoryDiagnosticsWindow::drawGateStatePanel()
    {
        if (!ImGui::CollapsingHeader("Gate State", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const memory::GateState gs = cpuSnapshot.gateState;
        ImVec4 stateColor;
        switch (gs)
        {
        case memory::GateState::Open:
            stateColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            break;
        case memory::GateState::Closed:
            stateColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            break;
        case memory::GateState::CriticalOverage:
            stateColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            break;
        default:
            stateColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            break;
        }

        ImGui::Text("Gate:");
        ImGui::SameLine();
        ImGui::TextColored(stateColor, "%s", memory::gateStateName(gs));

        ImGui::Text("Budget: %.2f GB",
            static_cast<float>(cpuSnapshot.budgetBytes) / GB);
        ImGui::Text("Tracked (transient+staging): %.2f MB",
            toMB(cpuSnapshot.totalTrackedBytes));
        ImGui::Text("Reserved (in-flight loads): %.2f MB",
            toMB(cpuSnapshot.totalReservedBytes));

        if (cpuSnapshot.deferredLoadCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                "Deferred loads: %u", cpuSnapshot.deferredLoadCount);
        }
        else
        {
            ImGui::Text("Deferred loads: 0");
        }
    }

    // -----------------------------------------------------------------------
    // VRAM Assets tab (VK-1539) — per-asset attribution published by the render
    // thread (GPUDrivenRenderer::publishVramAttribution) into VramAssetSnapshot.
    // -----------------------------------------------------------------------

    void MemoryDiagnosticsWindow::drawVramTab()
    {
        ImGui::Text("Textures: %.2f MB", toMB(vramSnapshot.textureTotalBytes));
        ImGui::SameLine();
        ImGui::TextDisabled("(reconciles with Culling Stats -> Texture Mip Streaming)");
        ImGui::Text("Meshes: %.2f MB   Virtual Texture: %.2f MB",
            toMB(vramSnapshot.meshTotalBytes), toMB(vramSnapshot.vtTotalBytes));
        ImGui::Separator();

        const char* filters[] = {"All", "Texture", "Mesh", "Virtual Texture"};
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::Combo("##VramFilter", &vramCategoryFilter, filters, IM_ARRAYSIZE(filters));

        if (vramSnapshot.rows.empty())
        {
            ImGui::TextDisabled("No resident VRAM assets yet (open a GPU-driven scene with streaming).");
            return;
        }

        if (ImGui::BeginTable("##VramAssets", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY,
            ImVec2(0, ImGui::GetContentRegionAvail().y)))
        {
            ImGui::TableSetupColumn("Asset");
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("VRAM (MB)",
                ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                ImGuiTableColumnFlags_PreferSortDescending, 90.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Sort the cached rows in place; producer already publishes bytes-desc, so the
            // default (VRAM column, descending) leaves them as the top-N largest view.
            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
            {
                if (sortSpecs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
                    std::stable_sort(vramSnapshot.rows.begin(), vramSnapshot.rows.end(),
                        [&spec, ascending](const memory::VramAssetRow& a, const memory::VramAssetRow& b)
                        {
                            bool less;
                            switch (spec.ColumnIndex)
                            {
                            case 1: less = static_cast<int>(a.category) < static_cast<int>(b.category); break;
                            case 2: less = a.bytes < b.bytes; break;
                            default: less = a.name < b.name; break;
                            }
                            return ascending ? less : !less;
                        });
                    sortSpecs->SpecsDirty = false;
                }
            }

            for (const auto& row : vramSnapshot.rows)
            {
                if (vramCategoryFilter > 0 && static_cast<int>(row.category) != (vramCategoryFilter - 1))
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", memory::vramAssetCategoryName(row.category));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", toMB(row.bytes));
            }

            ImGui::EndTable();
        }
    }

    // -----------------------------------------------------------------------
    // Snapshots tab (VK-1539) — named in-memory captures + two-capture diff.
    // -----------------------------------------------------------------------

    void MemoryDiagnosticsWindow::captureNow(const char* label)
    {
        memory::MemorySnapshotCapture cap;
        cap.label = label;
        cap.timestampUnixMs = static_cast<int64_t>(std::time(nullptr)) * 1000;

        // CPU axis (from the already-sampled cpuSnapshot).
        cap.cpuCategories.reserve(cpuSnapshot.categories.size());
        for (const auto& c : cpuSnapshot.categories)
            cap.cpuCategories.push_back(memory::CapturedCpuCat{c.name, c.kind, c.bytes, c.peak});
        cap.cpuTotalTrackedBytes = cpuSnapshot.totalTrackedBytes;
        cap.cpuBudgetBytes = cpuSnapshot.budgetBytes;

        // GPU heap aggregates (from the process-global GpuAllocationStats atomics).
        using Stats = memory::GpuAllocationStats;
        cap.gpuHeaps.push_back({"Device-Local",
            Stats::deviceLocalUsedBytes.load(std::memory_order_relaxed),
            Stats::deviceLocalCapacityBytes.load(std::memory_order_relaxed)});
        cap.gpuHeaps.push_back({"Host-Visible",
            Stats::hostVisibleUsedBytes.load(std::memory_order_relaxed),
            Stats::hostVisibleCapacityBytes.load(std::memory_order_relaxed)});
        cap.gpuHeaps.push_back({"Dedicated",
            Stats::dedicatedAllocatedBytes.load(std::memory_order_relaxed),
            Stats::dedicatedAllocatedBytes.load(std::memory_order_relaxed)});
        cap.gpuHeaps.push_back({"Staging",
            Stats::stagingRingUsed.load(std::memory_order_relaxed),
            Stats::stagingRingSize.load(std::memory_order_relaxed)});
        cap.vramBudgetBytes = Stats::vramBudgetBytes.load(std::memory_order_relaxed);
        cap.vramUsageBytes = Stats::vramUsageBytes.load(std::memory_order_relaxed);

        // Per-asset VRAM axis (from the already-sampled vramSnapshot).
        cap.vramAssets = vramSnapshot.rows;
        cap.vramTextureTotal = vramSnapshot.textureTotalBytes;
        cap.vramMeshTotal = vramSnapshot.meshTotalBytes;
        cap.vramVtTotal = vramSnapshot.vtTotalBytes;

        captures.push_back(std::move(cap));
    }

    void MemoryDiagnosticsWindow::drawSnapshotsTab()
    {
        ImGui::TextWrapped("Capture named snapshots and diff two of them to hunt leaks: "
                           "capture, load or unload a scene, capture again, then diff.");
        ImGui::Separator();

        ImGui::SetNextItemWidth(200);
        ImGui::InputTextWithHint("##CapLabel", "snapshot label", captureLabel, sizeof(captureLabel));
        ImGui::SameLine();
        if (ImGui::Button("Capture"))
            captureNow(captureLabel[0] != '\0' ? captureLabel : "snapshot");
        ImGui::SameLine();
        if (ImGui::Button("Clear All"))
        {
            captures.clear();
            diffA = diffB = -1;
        }

        if (captures.empty())
        {
            ImGui::TextDisabled("No captures yet.");
            return;
        }

        if (ImGui::BeginTable("##Captures", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, std::min(160.0f, 24.0f + captures.size() * 20.0f))))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableSetupColumn("Label");
            ImGui::TableSetupColumn("CPU MB", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("VRAM MB", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Export", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(captures.size()); ++i)
            {
                const memory::MemorySnapshotCapture& cap = captures[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", i);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(cap.label.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", toMB(cap.cpuTotalTrackedBytes));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", toMB(cap.vramUsageBytes));
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::SmallButton("CSV")) exportCaptureCsv(cap);
                ImGui::SameLine();
                if (ImGui::SmallButton("JSON")) exportCaptureJson(cap);
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (!lastExportPath.empty())
            ImGui::TextDisabled("Saved: %s", lastExportPath.c_str());

        // Diff selection — clamp indices to the current capture list.
        const int count = static_cast<int>(captures.size());
        if (diffA < 0 || diffA >= count) diffA = 0;
        if (diffB < 0 || diffB >= count) diffB = count - 1;

        std::vector<const char*> labels;
        labels.reserve(captures.size());
        for (const auto& c : captures)
            labels.push_back(c.label.c_str());

        ImGui::Separator();
        ImGui::TextUnformatted("Diff two captures:");
        ImGui::SetNextItemWidth(180);
        ImGui::Combo("Before (A)", &diffA, labels.data(), count);
        ImGui::SetNextItemWidth(180);
        ImGui::Combo("After (B)", &diffB, labels.data(), count);

        if (diffA == diffB)
        {
            ImGui::TextDisabled("Select two different captures to diff.");
            return;
        }

        const memory::MemorySnapshotDiff d = memory::diff(captures[diffA], captures[diffB]);
        ImGui::Separator();
        ImGui::TextColored(deltaColor(d.cpuTotalDelta), "CPU tracked delta: %+.2f MB",
            static_cast<double>(d.cpuTotalDelta) / MB);
        ImGui::TextColored(deltaColor(d.vramUsageDelta), "VRAM usage delta: %+.2f MB",
            static_cast<double>(d.vramUsageDelta) / MB);
        drawDeltaTable("##dCpu", "CPU Category Deltas", d.cpuDeltas);
        drawDeltaTable("##dHeap", "GPU Heap Deltas", d.heapDeltas);
        drawDeltaTable("##dAsset", "Per-Asset VRAM Deltas (Added / growth = potential leak)", d.assetDeltas);
    }

    void MemoryDiagnosticsWindow::exportCaptureCsv(const memory::MemorySnapshotCapture& cap)
    {
        const std::string path = "memory_capture_" + sanitizeLabel(cap.label) + ".csv";
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            vfLogWarning("MemoryDiagnostics: failed to open {} for export", path);
            return;
        }

        out << "section,name,bytes,extra\n";
        for (const auto& c : cap.cpuCategories)
            out << "cpu," << c.name << ',' << c.bytes << ',' << c.peak << "\n";
        for (const auto& h : cap.gpuHeaps)
            out << "gpu_heap," << h.name << ',' << h.usedBytes << ',' << h.capacityBytes << "\n";
        for (const auto& a : cap.vramAssets)
            out << "vram_asset," << a.name << ',' << a.bytes << ',' << memory::vramAssetCategoryName(a.category) << "\n";

        out.close();
        lastExportPath = path;
        vfLogInfo("MemoryDiagnostics: exported capture to {}", path);
    }

    void MemoryDiagnosticsWindow::exportCaptureJson(const memory::MemorySnapshotCapture& cap)
    {
        nlohmann::json j;
        j["schemaVersion"] = cap.schemaVersion;
        j["label"] = cap.label;
        j["timestampUnixMs"] = cap.timestampUnixMs;
        j["cpuTotalTrackedBytes"] = cap.cpuTotalTrackedBytes;
        j["cpuBudgetBytes"] = cap.cpuBudgetBytes;
        j["vramBudgetBytes"] = cap.vramBudgetBytes;
        j["vramUsageBytes"] = cap.vramUsageBytes;
        j["vramTextureTotal"] = cap.vramTextureTotal;
        j["vramMeshTotal"] = cap.vramMeshTotal;
        j["vramVtTotal"] = cap.vramVtTotal;
        for (const auto& c : cap.cpuCategories)
            j["cpuCategories"].push_back({{"name", c.name}, {"kind", memory::categoryKindName(c.kind)},
                                          {"bytes", c.bytes}, {"peak", c.peak}});
        for (const auto& h : cap.gpuHeaps)
            j["gpuHeaps"].push_back({{"name", h.name}, {"usedBytes", h.usedBytes},
                                     {"capacityBytes", h.capacityBytes}});
        for (const auto& a : cap.vramAssets)
            j["vramAssets"].push_back({{"name", a.name},
                                       {"category", memory::vramAssetCategoryName(a.category)},
                                       {"bytes", a.bytes}});

        const std::string path = "memory_capture_" + sanitizeLabel(cap.label) + ".json";
        std::ofstream out(path, std::ios::trunc);
        if (!out)
        {
            vfLogWarning("MemoryDiagnostics: failed to open {} for export", path);
            return;
        }
        out << j.dump(2);
        out.close();
        lastExportPath = path;
        vfLogInfo("MemoryDiagnostics: exported capture to {}", path);
    }
}
