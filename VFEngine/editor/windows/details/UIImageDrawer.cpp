#include "print/Log.hpp"
#include "UIImageDrawer.hpp"
#include "../scene/EntityDetailsPanel.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ui/UIEvents.hpp"
#include "nfd/FileDialog.hpp"
#include "asset/AssetRef.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "DrawerHelpers.hpp"
#include <imgui.h>
#include <fstream>

namespace windows::details
{
    namespace
    {
        // Assign a .vfImage to the component, reading the source dimensions
        // from the file header (1 byte file type + 12 bytes version, then
        // uint32 width, uint32 height).
        bool assignUIImageTexture(services::UIImageData& data, const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.good())
            {
                vfLogError("Selected texture file does not exist or cannot be read: {}", path);
                return false;
            }

            file.seekg(13, std::ios::beg);
            uint32_t w = 0, h = 0;
            file.read(reinterpret_cast<char*>(&w), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&h), sizeof(uint32_t));
            if (!file.good())
            {
                vfLogWarning("Failed to read .vfImage header (file too short): {}", path);
                return false;
            }

            data.textureRef = asset::AssetRef::fromPath(path);
            data.sourceWidth = w;
            data.sourceHeight = h;
            return true;
        }
    }

    bool UIImageDrawer::draw(services::EntityHandle handle)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::ui::HasUIImageComponentQuery hasQuery;
        hasQuery.entity = handle;
        bool hasImage = dispatcher.query(hasQuery);

        if (!hasImage)
        {
            return false;
        }

        events::ui::GetUIImageDataQuery dataQuery;
        dataQuery.entity = handle;
        auto dataOpt = dispatcher.query(dataQuery);

        if (!dataOpt.has_value())
        {
            return true;
        }

        ImGui::PushID("UIImageComponent");

        bool removeImage = false;
        bool isOpen = drawHeader(removeImage);

        if (isOpen)
        {
            ImGui::Indent(10.0f);

            services::UIImageData data = *dataOpt;
            bool changed = false;

            ImGui::TextDisabled("Screen-space image with texture and color tint");
            ImGui::Spacing();

            changed |= drawTexturePath(data);
            ImGui::Spacing();
            changed |= drawRenderTextureSource(data);
            ImGui::Spacing();
            changed |= drawColorTint(data);
            ImGui::Spacing();
            changed |= drawSliceSettings(data);

            if (changed)
            {
                events::ui::SetUIImageDataCommand cmd;
                cmd.entity = handle;
                cmd.imageData = data;
                dispatcher.execute(cmd);
            }

            ImGui::Unindent(10.0f);
        }

        ImGui::PopID();

        if (removeImage)
        {
            events::ui::RemoveUIImageComponentCommand cmd;
            cmd.entity = handle;
            dispatcher.execute(cmd);
        }

        return true;
    }

    bool UIImageDrawer::drawHeader(bool& outRemove)
    {
        EntityDetailsPanel::pushComponentHeaderStyle();
        bool isOpen = ImGui::CollapsingHeader("##UIImageHeader",
                                              ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine();
        ImGui::Text("UI Image");

        EntityDetailsPanel::pushRemoveButtonStyle();
        if (ImGui::Button("x##RemoveUIImage", ImVec2(18, 18)))
        {
            outRemove = true;
        }
        EntityDetailsPanel::popRemoveButtonStyle();
        EntityDetailsPanel::popComponentHeaderStyle();

        return isOpen;
    }

    bool UIImageDrawer::drawTexturePath(services::UIImageData& data)
    {
        bool changed = false;

        if (data.textureRef.isValid())
        {
            std::string filename = data.textureRef.resolve();
            auto lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                filename = filename.substr(lastSlash + 1);
            }
            ImGui::Text("Texture: %s", filename.c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture selected");
        }
        if (auto dropped = acceptAssetDropOnLastItem("UIImageTexDrop", {".vfimage"}))
        {
            changed = assignUIImageTexture(data, *dropped);
        }

        if (ImGui::Button("Select Texture##UIImage"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Image Files (*.vfImage)", L"*.vfImage"}});
            if (!path.empty())
            {
                changed = assignUIImageTexture(data, path) || changed;
            }
        }

        ImGui::SameLine();
        bool wasEmpty = !data.textureRef.isValid();
        if (wasEmpty) ImGui::BeginDisabled();
        if (ImGui::Button("Clear##UIImageTex"))
        {
            data.textureRef = asset::AssetRef::invalid();
            data.sourceWidth = 0;
            data.sourceHeight = 0;
            changed = true;
        }
        if (wasEmpty) ImGui::EndDisabled();

        return changed;
    }

    bool UIImageDrawer::drawRenderTextureSource(services::UIImageData& data)
    {
        return rttPicker.draw("UIImageRTT", data.renderTextureSourceName, data.renderTextureSource);
    }

    bool UIImageDrawer::drawColorTint(services::UIImageData& data)
    {
        bool changed = false;

        if (ColorEditRow("Color Tint##UIImage", &data.colorTint.x))
        {
            changed = true;
        }

        return changed;
    }

    bool UIImageDrawer::drawSliceSettings(services::UIImageData& data)
    {
        bool changed = false;

        const char* imageTypeNames[] = {"Simple", "Sliced", "Tiled"};
        int currentType = static_cast<int>(data.imageType);
        if (ImGui::Combo("Image Type##UIImage", &currentType, imageTypeNames, 3))
        {
            data.imageType = static_cast<uint8_t>(currentType);
            changed = true;
        }

        if (data.imageType != 0) // Sliced or Tiled
        {
            // Auto-detect dimensions if texture exists but dimensions are missing (old scenes)
            // Only attempt the read once per texture path to avoid file I/O every frame
            static std::string lastAutoReadPath;
            std::string resolvedTexPath = data.textureRef.resolve();
            if ((data.sourceWidth == 0 || data.sourceHeight == 0) && data.textureRef.isValid()
                && resolvedTexPath != lastAutoReadPath)
            {
                lastAutoReadPath = resolvedTexPath;
                std::ifstream texFile(resolvedTexPath, std::ios::binary);
                if (texFile.good())
                {
                    texFile.seekg(13, std::ios::beg);
                    uint32_t w = 0, h = 0;
                    texFile.read(reinterpret_cast<char*>(&w), sizeof(uint32_t));
                    texFile.read(reinterpret_cast<char*>(&h), sizeof(uint32_t));
                    bool readOk = texFile.good();
                    texFile.close();
                    if (readOk && w > 0 && h > 0)
                    {
                        data.sourceWidth = w;
                        data.sourceHeight = h;
                        changed = true;
                    }
                }
            }

            if (data.sourceWidth == 0 || data.sourceHeight == 0)
            {
                ImGui::TextDisabled("Select a texture to configure borders");
            }
            else
            {
                ImGui::Text("Source: %ux%u", data.sourceWidth, data.sourceHeight);

                float maxW = static_cast<float>(data.sourceWidth);
                float maxH = static_cast<float>(data.sourceHeight);

                if (ImGui::DragFloat("Border Left##UIImage", &data.border.x, 1.0f, 0.0f, maxW, "%.0f"))
                    changed = true;
                if (ImGui::DragFloat("Border Right##UIImage", &data.border.y, 1.0f, 0.0f, maxW, "%.0f"))
                    changed = true;
                if (ImGui::DragFloat("Border Top##UIImage", &data.border.z, 1.0f, 0.0f, maxH, "%.0f"))
                    changed = true;
                if (ImGui::DragFloat("Border Bottom##UIImage", &data.border.w, 1.0f, 0.0f, maxH, "%.0f"))
                    changed = true;
            }
        }

        return changed;
    }
}
