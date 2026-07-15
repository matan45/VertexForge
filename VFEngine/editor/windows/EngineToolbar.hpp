#pragma once

struct ImGuiViewport;

namespace windows
{
    class EngineToolbar
    {
    private:
        float height = 36.0f;
        float buttonWidth = 60.0f;
        bool actionsRegistered = false;

        void registerHotkeys();
        void handleHotkeys();
        void drawPlayControls();
        void drawAudioMuteToggle();
        void drawBuildActions();

    public:
        void draw(const ImGuiViewport* viewport);
        float getHeight() const { return height; }
    };
}
