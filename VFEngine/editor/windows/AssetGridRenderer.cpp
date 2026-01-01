#include "AssetGridRenderer.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include <IconsFontAwesome6.h>
#include <algorithm>

namespace windows
{
    void AssetGridRenderer::ensureIconsLoaded()
    {
        if (!iconsLoaded)
        {
            loadIconAtlas();
        }
    }

    void AssetGridRenderer::loadIconAtlas()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::render::LoadEditorTextureCommand cmd;
        cmd.path = "../../resources/editor/atlasIcons.vfImage";
        iconAtlas = dispatcher.execute(cmd);

        iconsLoaded = true;
    }

    std::pair<ImVec2, ImVec2> AssetGridRenderer::getAtlasUV(AtlasIcon icon)
    {
        uint32_t index = static_cast<uint32_t>(icon);
        constexpr uint32_t maxIndex = ATLAS_GRID_SIZE * ATLAS_GRID_SIZE;

        if (index >= maxIndex)
        {
            index = static_cast<uint32_t>(AtlasIcon::File);
        }

        float gridSize = static_cast<float>(ATLAS_GRID_SIZE);
        float tileSize = 1.0f / gridSize;

        float col = static_cast<float>(index % ATLAS_GRID_SIZE);
        float row = static_cast<float>(index / ATLAS_GRID_SIZE);

        ImVec2 uv0(col * tileSize, row * tileSize);
        ImVec2 uv1((col + 1.0f) * tileSize, (row + 1.0f) * tileSize);

        return {uv0, uv1};
    }

    bool AssetGridRenderer::matchesSearchQuery(const Asset& asset, const std::string& searchQuery)
    {
        if (searchQuery.empty())
        {
            return true;
        }

        std::string assetNameLower = StringUtil::toLower(asset.name);
        std::string searchQueryLower = StringUtil::toLower(searchQuery);

        return assetNameLower.find(searchQueryLower) != std::string::npos;
    }

    AssetClickResult AssetGridRenderer::draw(
        const std::vector<Asset>& assets,
        const fs::path& selectedFile,
        const std::string& searchQuery)
    {
        AssetClickResult result;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float cellSize = PADDING + THUMBNAIL_SIZE;
        int columnCount = (std::max)(1, static_cast<int>(panelWidth / cellSize));
        ImGui::Columns(columnCount, "", false);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        for (const auto& asset : assets)
        {
            if (matchesSearchQuery(asset, searchQuery))
            {
                bool isSelected = (selectedFile == fs::path(asset.path));
                drawAssetItem(asset, isSelected, result);

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    result.wasClicked = true;
                    result.clickedPath = asset.path;
                    result.clickedType = asset.type;

                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                        && asset.type != AssetType::Scene
                        && (asset.type != AssetType::Other || !fs::is_directory(asset.path)))
                    {
                        result.wasDoubleClicked = true;
                    }
                }
            }
        }
        ImGui::PopStyleColor();

        return result;
    }

    void AssetGridRenderer::drawAssetItem(const Asset& asset, bool isSelected, AssetClickResult& result)
    {
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        float itemWidth = THUMBNAIL_SIZE + PADDING;
        float itemHeight = THUMBNAIL_SIZE + ImGui::GetTextLineHeightWithSpacing() + 4.0f;

        if (isSelected)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 highlightColor = IM_COL32(70, 130, 180, 100);
            drawList->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
                highlightColor,
                4.0f
            );
        }

        if (!iconAtlas.isValid())
        {
            ImGui::NextColumn();
            return;
        }

        AtlasIcon icon = AtlasIcon::File;
        bool isFolder = false;

        switch (asset.type)
        {
            using enum windows::AssetType;
        case Texture:
            icon = AtlasIcon::Texture;
            break;
        case HDR:
            icon = AtlasIcon::Hdr;
            break;
        case Scene:
            icon = AtlasIcon::Scene;
            break;
        case Model:
            icon = AtlasIcon::Mesh;
            break;
        case Audio:
            icon = AtlasIcon::Audio;
            break;
        case Animation:
            icon = AtlasIcon::Animation;
            break;
        case Shader:
            icon = AtlasIcon::Glsl;
            break;
        case Material:
        case MaterialInstance:
            icon = AtlasIcon::Material;
            break;
        case Prefab:
            icon = AtlasIcon::Prefab;
            break;
        case Other:
            if (fs::is_directory(asset.path))
            {
                icon = AtlasIcon::Folder;
                isFolder = true;
            }
            else
            {
                icon = AtlasIcon::File;
            }
            break;
        }

        auto [uv0, uv1] = getAtlasUV(icon);

        if (isFolder)
        {
            ImGui::BeginGroup();
            std::string folderName = asset.name;
            ImGui::ImageButton(folderName.c_str(), iconAtlas.imguiDescriptorSet,
                ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE), uv0, uv1);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                result.pendingNavigation = asset.path;
            }
            ImGui::TextWrapped("%s", folderName.c_str());
            ImGui::EndGroup();
        }
        else
        {
            ImGui::BeginGroup();
            ImGui::Image(iconAtlas.imguiDescriptorSet, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE), uv0, uv1);

            if (asset.type == AssetType::Prefab && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("DND_PREFAB_PATH", asset.path.c_str(), asset.path.size() + 1);
                ImGui::Text("Instantiate %s", asset.name.c_str());
                ImGui::EndDragDropSource();
            }

            if (asset.type == AssetType::Texture && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("DND_TEXTURE_PATH", asset.path.c_str(), asset.path.size() + 1);
                ImGui::Text("Texture: %s", asset.name.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TextWrapped("%s", asset.name.c_str());
            ImGui::EndGroup();
        }

        ImGui::NextColumn();
    }
}
