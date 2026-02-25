#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class BillboardDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTexturePath(services::BillboardData& data);
        bool drawRenderTextureSource(services::BillboardData& data);
        bool drawSizeInput(services::BillboardData& data);
        bool drawColorTint(services::BillboardData& data);

        void refreshRTTCandidates();

        std::vector<services::EntityHandle> rttCandidates;
        std::vector<std::string> rttCandidateNames;
        int selectedRTTIdx = -1;
        bool rttNeedsRefresh = true;
    };
}
