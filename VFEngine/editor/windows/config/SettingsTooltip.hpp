#pragma once
#include <imgui.h>

namespace windows
{
    inline void drawSettingTooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", text);
    }
}
