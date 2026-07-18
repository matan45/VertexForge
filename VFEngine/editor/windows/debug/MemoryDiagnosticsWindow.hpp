#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "memory/GpuMemorySnapshot.hpp"
#include "memory/VramAssetSnapshot.hpp"
#include "memory/MemorySnapshotCapture.hpp"
#include "cpumem/CpuMemorySnapshot.hpp"

namespace windows
{
    class MemoryDiagnosticsWindow
    {
    public:
        void draw();
        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible; }

    private:
        bool visible = false;

        // --- live sampling state ---
        static constexpr int SAMPLE_COUNT = 256;
        static constexpr float REFRESH_INTERVAL = 0.25f; // seconds between samples

        struct SampleRing
        {
            float values[SAMPLE_COUNT] = {};
            int head = 0;
            void push(float v) { values[head] = v; head = (head + 1) % SAMPLE_COUNT; }
        };

        float refreshTimer = 0.0f;

        // --- GPU sampling ---
        SampleRing deviceUsedMB;
        SampleRing hostUsedMB;
        SampleRing deviceFragPct;
        memory::GpuMemorySnapshot snapshot; // refreshed on the sample tick
        std::string lastExportPath;

        // --- CPU sampling ---
        SampleRing cpuTrackedMB;
        memory::CpuMemorySnapshotData cpuSnapshot; // refreshed on the sample tick

        // --- VRAM per-asset attribution (VK-1539) ---
        memory::VramAssetSnapshot vramSnapshot; // refreshed on the sample tick
        int vramCategoryFilter = 0;                 // 0=All, then Texture/Mesh/VirtualTexture
        uint64_t lastSortedVramGeneration = 0;      // re-sort the table only when this changes

        // --- named snapshot captures + diff (VK-1539) ---
        std::vector<memory::MemorySnapshotCapture> captures; // session-only
        char captureLabel[64] = {};
        int diffA = -1;
        int diffB = -1;

        // Cached two-capture diff, recomputed only when the selection or capture count changes.
        memory::MemorySnapshotDiff cachedDiff;
        int cachedDiffA = -1;
        int cachedDiffB = -1;
        size_t cachedDiffCount = 0;

        void sample();

        // GPU draw methods
        void drawBudgetBar();
        void drawGpuAllocationPanel();
        void drawGpuBlocksPanel();
        void drawTimeSeriesPanel();
        void drawFragmentationMapPanel();
        void drawStagingPanel();
        void drawPoolConfigPanel();
        void drawLeakDetectionPanel();
        void exportSnapshotToCsv();

        // CPU draw methods
        void drawCpuTab();
        void drawCpuBudgetBar();
        void drawCpuProcessMemoryPanel();
        void drawCpuCategoryTable();
        void drawCpuTimeSeriesPanel();
        void drawGateStatePanel();

        // VRAM Assets + Snapshots tabs (VK-1539)
        void drawVramTab();
        void drawSnapshotsTab();
        void captureNow(const char* label);
        void exportCaptureCsv(const memory::MemorySnapshotCapture& cap, int index);
        void exportCaptureJson(const memory::MemorySnapshotCapture& cap, int index);
    };
}
