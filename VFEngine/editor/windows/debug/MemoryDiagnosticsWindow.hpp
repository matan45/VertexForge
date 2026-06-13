#pragma once

#include <cstdint>
#include <string>
#include "memory/GpuMemorySnapshot.hpp"

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
        SampleRing deviceUsedMB;
        SampleRing hostUsedMB;
        SampleRing deviceFragPct;
        memory::GpuMemorySnapshot snapshot; // refreshed on the sample tick
        std::string lastExportPath;

        void sample();
        void drawBudgetBar();
        void drawGpuAllocationPanel();
        void drawGpuBlocksPanel();
        void drawTimeSeriesPanel();
        void drawFragmentationMapPanel();
        void drawStagingPanel();
        void drawPoolConfigPanel();
        void drawLeakDetectionPanel();
        void exportSnapshotToCsv();
    };
}
