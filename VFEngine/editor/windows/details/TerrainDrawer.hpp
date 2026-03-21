#pragma once
#include "data/EntityHandle.hpp"
#include "data/TerrainData.hpp"
#include "nfd/FileDialog.hpp"
#include "threading/JobSystem.hpp"
#include <future>
#include <string>

namespace windows::details {

    class TerrainDrawer {
    public:
        bool draw(services::EntityHandle handle);

    private:
        void drawInfo(const services::TerrainData& terrain);
        void drawSaveLoad(services::EntityHandle handle, const services::TerrainData& terrain);
        void drawGridExpansion(services::EntityHandle handle);
        void drawStreaming(services::EntityHandle handle);
        void drawPhysics(services::EntityHandle handle, const services::TerrainData& terrain);
        void drawSVTBake(services::EntityHandle handle, const services::TerrainData& terrain);

        void startSave(services::EntityHandle handle, const std::string& path);
        void startSaveAs(services::EntityHandle handle);
        void startLoad();
        void pollSaveResult(services::EntityHandle handle);

        std::future<bool> pendingSave;
        bool isSaving = false;
        bool isBaking = false;
        std::string saveStatusMessage;
        int statusFrameCounter = 0;
        nfd::FileDialog fileDialog;

        int pendingTileX = 0;
        int pendingTileZ = 0;
    };

}
