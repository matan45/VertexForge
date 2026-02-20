#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "types/NavmeshTypes.hpp"
#include "../../services/events/EventTypes.hpp"

namespace windows
{
    class NavmeshWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        types::NavmeshBakeSettings settings;
        events::SubscriptionToken bakeCompleteToken;
        events::SubscriptionToken navmeshClearedToken;

        void drawBakeSettings();
        void drawAgentSection();
        void drawRegionSection();
        void drawPolygonSection();
        void drawFilterSection();
        void drawActions();
        void drawDebugSection();

        void pushNavmeshDebugMesh();

    public:
        NavmeshWindow();
        ~NavmeshWindow();

        void draw() override;
        void show();
    };
}
