#pragma once

namespace windows
{
    class CullingStatsWindow
    {
    private:
        bool visible = false;

        void drawGPUPipelineStatus();

    public:
        void draw();

        void toggle() { visible = !visible; }
        bool isVisible() const { return visible; }
    };
}
