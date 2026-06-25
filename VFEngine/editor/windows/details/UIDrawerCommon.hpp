#pragma once
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <string>

namespace windows::details
{
    // Shared texture-slot row used by the UI widget drawers (button / checkbox / slider /
    // progress bar) in the Details panel. Renders the label, the current texture's filename,
    // and Select / Clear buttons backed by a .vfImage file dialog. `uniqueId` is the full ImGui
    // id segment placed after "##" (callers prefix it per-widget, e.g. "UIBtn_normal") so each
    // drawer keeps its previously distinct, stable widget ids.
    inline bool drawUITextureSlot(const char* label, asset::AssetRef& textureRef, const char* uniqueId)
    {
        bool changed = false;

        ImGui::Text("%s", label);

        if (textureRef.isValid())
        {
            std::string filename = textureRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", filename.c_str());
        }

        char selectId[64];
        std::snprintf(selectId, sizeof(selectId), "Select##%s", uniqueId);
        if (ImGui::Button(selectId))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
            if (!path.empty())
            {
                std::ifstream file(path);
                if (file.good())
                {
                    file.close();
                    textureRef = asset::AssetRef::fromPath(path);
                    changed = true;
                }
                else
                {
                    vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                }
            }
        }

        ImGui::SameLine();
        bool wasEmpty = !textureRef.isValid();
        if (wasEmpty) ImGui::BeginDisabled();
        char clearId[64];
        std::snprintf(clearId, sizeof(clearId), "Clear##%s", uniqueId);
        if (ImGui::Button(clearId))
        {
            textureRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }
}
