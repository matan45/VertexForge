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
                  services::EntityHandle& renderTextureSource,
                  services::ComponentTypeId filter = services::ComponentTypeId::RenderTexture,
                  const char* comboLabel = "RTT Source",
                  const char* tooltip = "Entity with RenderTextureComponent + CameraComponent.\n"
                                        "Displays camera feed during play mode.");

    private:
        void refresh(services::ComponentTypeId filter);
        void syncSelection(const std::string& currentName);

        std::vector<services::EntityHandle> candidates;
        std::vector<std::string> candidateNames;
        int selectedIdx = -1;
        bool needsRefresh = true;
    };
}
