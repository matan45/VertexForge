#pragma once
#include <imgui.h>
#include <components/TextEffects.hpp>

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

    // VK-1635 outline / drop shadow / glow, shared by UILabelDrawer and TextDrawer.
    // idSuffix disambiguates the ImGui IDs between the two inspectors.
    //
    // Note there are no enable checkboxes: an effect is live iff its distance is non-zero
    // (and its colour is not fully transparent), which is why each distance leads its
    // group. Zeroing the distance is how you turn one off.
    inline bool drawTextEffects(components::TextEffectSettings& effects, const char* idSuffix)
    {
        bool changed = false;

        ImGui::PushID(idSuffix);
        if (ImGui::TreeNodeEx("Effects", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            ImGui::TextDisabled("Outline");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            changed |= ImGui::DragFloat("Width##Outline", &effects.outlineWidth,
                                        0.05f, 0.0f, 16.0f, "%.2f px");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("0 disables the outline. Capped in the shader by the font's\n"
                                  "baked field range - re-import with a larger MTSDF px range\n"
                                  "or SDF spread if you need a thicker one.");
            }
            changed |= ColorEditRow("Color##Outline", &effects.outlineColor.x);

            ImGui::Spacing();
            ImGui::TextDisabled("Drop shadow");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            changed |= ImGui::DragFloat2("Offset##Shadow", &effects.shadowOffset.x,
                                         0.1f, -32.0f, 32.0f, "%.1f px");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Offset (0, 0) disables the shadow. +X right, +Y down.");
            }
            changed |= ColorEditRow("Color##Shadow", &effects.shadowColor.x);

            ImGui::Spacing();
            ImGui::TextDisabled("Glow");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            changed |= ImGui::DragFloat("Range##Glow", &effects.glowRange,
                                        0.05f, 0.0f, 32.0f, "%.2f px");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("0 disables the glow. Same field-range cap as the outline.");
            }
            changed |= ColorEditRow("Color##Glow", &effects.glowColor.x);

            ImGui::TreePop();
        }
        ImGui::PopID();

        return changed;
    }
}
