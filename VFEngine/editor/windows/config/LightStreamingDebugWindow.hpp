#pragma once
#include "../../../graphics/render/lighting/LightStreamManager.hpp"
#include "../../../core/controllers/imguiHandler/ImguiWindow.hpp"

namespace windows
{
    class LightStreamingDebugWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        render::lighting::LightStreamingConfig config;
        render::lighting::LightStreamingStats stats;
        bool configLoaded = false;
        bool configDirty = false;

        void drawStatsSection();
        void drawConfigSection();
        void drawPoolVisualization();
        void loadConfig();
        void applyConfig();

    public:
        void draw() override;
        void show();
        void notifySceneLoaded();
    };
}
