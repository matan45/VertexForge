#pragma once

// Gameplay Ability System (VK-816) — small shared ImGui form helpers for the
// GAS authoring windows. Header-only; depends only on imgui + std.

#include <imgui.h>

#include <cstdio>
#include <string>
#include <vector>

namespace gas::editorui
{
    // Bind an ImGui::InputText to a std::string (fixed scratch buffer).
    inline bool inputText(const char* label, std::string& s, ImGuiInputTextFlags flags = 0)
    {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", s.c_str());
        if (ImGui::InputText(label, buf, sizeof(buf), flags))
        {
            s = buf;
            return true;
        }
        return false;
    }

    inline bool floatField(const char* label, float& v, float step = 0.1f)
    {
        return ImGui::InputFloat(label, &v, step, step * 10.0f, "%.3f");
    }

    // Editable list of strings: one InputText per row + remove, plus Add.
    inline bool stringList(const char* id, std::vector<std::string>& items, const char* addLabel = "Add")
    {
        bool changed = false;
        ImGui::PushID(id);
        int removeIdx = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            ImGui::PushID(i);
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", items[i].c_str());
            if (ImGui::InputText("##item", buf, sizeof(buf)))
            {
                items[i] = buf;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            items.erase(items.begin() + removeIdx);
            changed = true;
        }
        if (ImGui::SmallButton(addLabel))
        {
            items.emplace_back();
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    // Combo over a fixed option list bound to a string value (used with the
    // enum<->string helpers in GASAssets.hpp).
    inline bool combo(const char* label, std::string& value, const std::vector<const char*>& options)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, value.c_str()))
        {
            for (const char* opt : options)
            {
                const bool sel = (value == opt);
                if (ImGui::Selectable(opt, sel))
                {
                    value = opt;
                    changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }
}
