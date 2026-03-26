#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "types/NavmeshTypes.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/events/navmesh/NavmeshEvents.hpp"
#include "nfd/FileDialog.hpp"

namespace windows
{
    class NavmeshWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        bool visible = false;
        types::NavmeshBakeSettings settings;
        events::SubscriptionToken bakeCompleteToken;
        events::SubscriptionToken tileUpdatedToken;
        nfd::FileDialog fileDialog;

        // Streaming config cache
        events::navmesh::NavmeshStreamingConfig streamingConfig;

        // Tile status cache (refreshed on notification, not every frame)
        std::vector<events::navmesh::NavmeshTileStatusInfo> cachedTileStatuses;
        bool tileStatusDirty = true;

        void drawBakeSettings();
        void drawAgentSection();
        void drawRegionSection();
        void drawPolygonSection();
        void drawFilterSection();
        void drawAreaCosts();
        void drawLodSettings();
        void drawActions();
        void drawTileStatus();
        void drawStreamingConfig();

        void pushNavmeshDebugMesh();

    public:
        NavmeshWindow();
        ~NavmeshWindow();

        void draw() override;
        void show();
    };
}
