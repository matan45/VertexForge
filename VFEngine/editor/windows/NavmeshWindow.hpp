#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "types/NavmeshTypes.hpp"
#include "../../services/events/EventTypes.hpp"
#include "nfd/FileDialog.hpp"

namespace windows
{
    class NavmeshWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        types::NavmeshBakeSettings settings;
        events::SubscriptionToken bakeCompleteToken;
        nfd::FileDialog fileDialog;

        void drawBakeSettings();
        void drawAgentSection();
        void drawRegionSection();
        void drawPolygonSection();
        void drawFilterSection();
        void drawActions();

        void pushNavmeshDebugMesh();

    public:
        NavmeshWindow();
        ~NavmeshWindow();

        void draw() override;
        void show();
    };
}
