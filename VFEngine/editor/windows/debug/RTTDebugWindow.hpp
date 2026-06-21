#pragma once

namespace windows
{
    // VK-1413: lists every active render-texture controller plus an advisory
    // EveryFrame budget warning. Editor-only debug overlay.
    class RTTDebugWindow
    {
    private:
        bool visible = false;

    public:
        void draw();

        void show() { visible = true; }
        void toggle() { visible = !visible; }
        bool isVisible() const { return visible; }
    };
}
