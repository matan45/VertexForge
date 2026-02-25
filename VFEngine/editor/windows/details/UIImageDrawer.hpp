#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class UIImageDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(bool& outRemove);
        bool drawTexturePath(services::UIImageData& data);
        bool drawRenderTextureSource(services::UIImageData& data);
        bool drawColorTint(services::UIImageData& data);

        void refreshRTTCandidates();

        std::vector<services::EntityHandle> rttCandidates;
        std::vector<std::string> rttCandidateNames;
        int selectedRTTIdx = -1;
        bool rttNeedsRefresh = true;
    };
}
