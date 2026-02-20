#pragma once

#include "types/NavmeshTypes.hpp"

namespace windows
{
    class NavmeshWindow
    {
    private:
        bool visible = false;
        types::NavmeshBakeSettings settings;

        void drawBakeSettings();
        void drawAgentSection();
        void drawRegionSection();
        void drawPolygonSection();
        void drawFilterSection();
        void drawActions();
        void drawDebugSection();

    public:
        void draw();
        void show();
    };
}
