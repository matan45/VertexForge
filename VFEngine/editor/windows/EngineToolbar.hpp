#pragma once

struct ImGuiViewport;

namespace windows
{
    class EngineToolbar
    {
    private:
        float height = 36.0f;
        float buttonWidth = 60.0f;

        void drawPlayControls();
        void drawBuildActions();

    public:
        void draw(const ImGuiViewport* viewport);
        float getHeight() const { return height; }
    };
}
