#pragma once

#include <cstdint>

namespace windows
{
    class MemoryDiagnosticsWindow
    {
    private:
        bool visible = false;

        void drawGpuAllocationPanel();
        void drawGpuBlocksPanel();
        void drawStagingPanel();
        void drawPoolConfigPanel();
        void drawLeakDetectionPanel();

    public:
        void draw();
        void show() { visible = true; }
        void hide() { visible = false; }
        bool isVisible() const { return visible; }
    };
}
