#pragma once
#include "../../../graphics/render/gi/GITypes.hpp"
#include "../../../core/controllers/imguiHandler/ImguiWindow.hpp"

namespace windows
{
    class GIConfigWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        render::gi::GISettings settings;
        bool settingsLoaded = false;
        bool isDirty = false;

        void drawQualitySection();
        void drawSSGISection();
        void drawProbeSection();
        void drawDebugSection();
        void drawStatsSection();
        void loadSettings();
        void applySettings();
        void resetToDefaults();

    public:
        void draw() override;
        void show();
        void notifySceneLoaded();
    };
}
