#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "data/VFXTypes.hpp"
#include <unordered_map>

namespace windows::details
{
    class VFXDrawer
    {
    private:
        // Maps entity handle ID to VFX preview instance (for editor preview, separate from play mode)
        std::unordered_map<uint64_t, services::VFXInstanceId> vfxPreviewInstances;

    public:
        bool draw(services::EntityHandle handle);
        void clearInstances();

    private:
        bool drawHeader(bool& outRemove);
        bool drawVFXFilePath(services::VFXData& vfxData);
        bool drawSettings(services::VFXData& vfxData);
        void drawPlaybackControls(services::EntityHandle handle, const services::VFXData& vfxData);
    };
}
