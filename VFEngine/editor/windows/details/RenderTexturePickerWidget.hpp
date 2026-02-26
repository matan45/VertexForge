#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class RenderTexturePickerWidget
    {
    public:
        bool draw(const char* imguiId,
                  std::string& renderTextureSourceName,
                  services::EntityHandle& renderTextureSource);

    private:
        void refresh();
        void syncSelection(const std::string& currentName);

        std::vector<services::EntityHandle> candidates;
        std::vector<std::string> candidateNames;
        int selectedIdx = -1;
        bool needsRefresh = true;
    };
}
