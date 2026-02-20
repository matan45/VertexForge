#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "types/NavmeshTypes.hpp"

namespace windows
{
    class NavmeshWindow : public controllers::imguiHandler::ImguiWindow
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
        void draw() override;
        void show();
    };
}
