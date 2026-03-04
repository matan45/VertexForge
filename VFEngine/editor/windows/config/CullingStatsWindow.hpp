#pragma once

namespace windows
{
    class CullingStatsWindow
    {
    private:
        bool visible = false;

    public:
        void draw();

        void toggle() { visible = !visible; }
        bool isVisible() const { return visible; }
    };
}
