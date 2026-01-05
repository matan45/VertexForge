#pragma once

namespace windows
{
    class CullingStatsWindow
    {
    private:
        bool visible = false;

    public:
        void draw();

        void show() { visible = true; }
        void hide() { visible = false; }
        void toggle() { visible = !visible; }
        bool isVisible() const { return visible; }
    };
}
