#pragma once
#include <imgui.h>

namespace windows::details
{
    // Width-responsive ColorEdit4: shrinks the widget so its right-side label never clips in a
    // narrow inspector pane. In a wide pane this is the natural width (avail - label), so the
    // Details panel is unaffected.
    inline bool ColorEditRow(const char* label, float* col, ImGuiColorEditFlags flags = 0)
    {
        const ImGuiStyle& s = ImGui::GetStyle();
        const float labelW = (label && label[0] != '\0' && label[0] != '#')
            ? ImGui::CalcTextSize(label, nullptr, true).x + s.ItemInnerSpacing.x : 0.0f;
        float w = ImGui::GetContentRegionAvail().x - labelW;
        if (w < 50.0f) w = 50.0f;
        ImGui::SetNextItemWidth(w);
        return ImGui::ColorEdit4(label, col, flags);
    }
}
