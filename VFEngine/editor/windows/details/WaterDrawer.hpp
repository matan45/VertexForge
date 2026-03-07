#pragma once
#include "data/EntityHandle.hpp"
#include "nfd/FileDialog.hpp"
#include <string>

namespace windows::details {

    class WaterDrawer {
    public:
        bool draw(services::EntityHandle handle);

    private:
        void startSave(services::EntityHandle handle, const std::string& path);
        void startSaveAs(services::EntityHandle handle);
        void startLoad();

        int pendingTileX = 0;
        int pendingTileZ = 0;
        std::string statusMessage;
        int statusFrameCounter = 0;

        // Save/Load
        nfd::FileDialog fileDialog;

        // Streaming UI state
        float streamingLoadRadius = 200.0f;
        float streamingUnloadRadius = 250.0f;
        int streamingMaxLoads = 4;
        int streamingMaxUnloads = 4;
        bool streamingConfigLoaded = false;
    };

}
