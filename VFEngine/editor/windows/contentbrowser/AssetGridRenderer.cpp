#include "AssetGridRenderer.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "../../dragdrop/DragDropManager.hpp"
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
        const std::string& searchQuery)
    {
        AssetClickResult result;

        // Collect all selected paths for multi-selection drag
        std::vector<std::string> selectedPaths;
        for (const auto& asset : assets)
        {
            if (asset.isSelected)
            {
                selectedPaths.push_back(asset.path);
            }
        }

        float panelWidth = ImGui::GetContentRegionAvail().x;
        float cellSize = PADDING + THUMBNAIL_SIZE;
        int columnCount = (std::max)(1, static_cast<int>(panelWidth / cellSize));
        ImGui::Columns(columnCount, "", false);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        for (const auto& asset : assets)
        {
            if (matchesSearchQuery(asset, searchQuery))
            {
                // Use Asset.isSelected for multi-selection support
                drawAssetItem(asset, asset.isSelected, selectedPaths, result);

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

    void AssetGridRenderer::drawAssetItem(const Asset& asset, bool isSelected, const std::vector<std::string>& selectedPaths, AssetClickResult& result)
    {
        // Determine which paths to drag: all selected if this item is selected, otherwise just this item
        std::vector<std::string> singlePath = {asset.path};
        const std::vector<std::string>& pathsToDrag = (isSelected && selectedPaths.size() > 1)
            ? selectedPaths
            : singlePath;
        ImGui::PushID(asset.path.c_str());
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        float itemWidth = THUMBNAIL_SIZE + PADDING;
        float itemHeight = THUMBNAIL_SIZE + ImGui::GetTextLineHeightWithSpacing() + 4.0f;

        // Apply dimming for cut items
        float alphaMultiplier = asset.isCut ? DragDropColors::CUT_ITEM_ALPHA : 1.0f;

        if (isSelected)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImU32 highlightColor = IM_COL32(70, 130, 180, static_cast<int>(100 * alphaMultiplier));
            drawList->AddRectFilled(
                cursorPos,
                ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
                highlightColor,
                4.0f
            );
        }

        if (!iconAtlas.isValid())
        {
            ImGui::PopID();
            ImGui::NextColumn();
            return;
        }

        // Push alpha for cut items
        if (asset.isCut)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaMultiplier);
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
        case Script:
            icon = AtlasIcon::Mtype;
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

            // Unified drag source for folders (for content browser operations)
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                DragDropManager::instance().beginDrag(pathsToDrag);
                DragDropManager::instance().setDragPayload();
                DragDropManager::instance().drawDragPreview();
                ImGui::EndDragDropSource();
            }

            // Drop target for folders
            if (ImGui::BeginDragDropTarget())
            {
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                ImRect dropRect(min, max);

                bool isValid = DragDropManager::instance().isValidDropTarget(asset.path);
                DragDropManager::drawDropTargetHighlight(dropRect, isValid);

                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_CONTENT_BROWSER);
                if (payload && isValid)
                {
                    DragDropManager::instance().acceptDrop(asset.path);
                }
                ImGui::EndDragDropTarget();
            }

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

            // Unified drag source for files (for content browser operations)
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                DragDropManager::instance().beginDrag(pathsToDrag);
                DragDropManager::instance().setDragPayload();
                DragDropManager::instance().drawDragPreview();
                ImGui::EndDragDropSource();
            }


            ImGui::TextWrapped("%s", asset.name.c_str());
            ImGui::EndGroup();
        }

        // Pop alpha style for cut items
        if (asset.isCut)
        {
            ImGui::PopStyleVar();
        }

        ImGui::PopID();
        ImGui::NextColumn();
    }
}
