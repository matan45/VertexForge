#pragma once
#include "data/EntityHandle.hpp"
#include "data/WaterData.hpp"
#include "nfd/FileDialog.hpp"
#include <string>

namespace windows::details
{
    class WaterDrawer
    {
    private:
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

    public:
        bool draw(services::EntityHandle handle);

    private:
        void drawSaveLoadSection(services::EntityHandle handle, const services::WaterData& data);
        void drawGridExpansionSection(services::EntityHandle handle);
        void drawStreamingSection(services::EntityHandle handle);

        void startSave(services::EntityHandle handle, const std::string& path);
        void startSaveAs(services::EntityHandle handle);
        void startLoad();
    };
}
