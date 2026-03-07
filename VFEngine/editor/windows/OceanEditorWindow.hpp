#pragma once

#include "data/WaterData.hpp"

namespace windows
{
    class OceanEditorWindow
    {
    private:
        bool visible = false;

        services::OceanFFTConfigData config;
        bool oceanActive = false;
        bool configDirty = false;

    public:
        void draw();
        void show();

    private:
        void drawConfigSection();
        void refreshState();
        void applyConfig();
    };
}
