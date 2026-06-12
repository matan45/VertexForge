#include "AssetGridRenderer.hpp"
#include "AssetQueryParser.hpp"
#include "string/StringUtil.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/RenderEvents.hpp"
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
        constexpr uint32_t maxIndex = ATLAS_COLS * ATLAS_ROWS;

        if (index >= maxIndex)
        {
            index = static_cast<uint32_t>(AtlasIcon::File);
        }

        float cols = static_cast<float>(ATLAS_COLS);
        float rows = static_cast<float>(ATLAS_ROWS);
        float tileW = 1.0f / cols;
        float tileH = 1.0f / rows;

        float col = static_cast<float>(index % ATLAS_COLS);
        float row = static_cast<float>(index / ATLAS_COLS);

        ImVec2 uv0(col * tileW, row * tileH);
        ImVec2 uv1((col + 1.0f) * tileW, (row + 1.0f) * tileH);

        return {uv0, uv1};
    }

    bool AssetGridRenderer::matchesFilter(const Asset& asset, const AssetFilter& filter, const ResolvedAssetFilter& resolved)
    {
        if (filter.typeFilter && asset.type != *filter.typeFilter)
        {
            if (!asset.isDirectory)
                return false;
        }

        // Plain terms apply to everything, folders included.
        if (!resolved.terms.empty())
        {
            std::string assetNameLower = StringUtil::toLower(asset.name);
            for (const auto& term : resolved.terms)
            {
                if (assetNameLower.find(term) == std::string::npos)
                    return false;
            }
        }

        // Structured tokens constrain files only; folders stay navigable.
        if (!asset.isDirectory)
        {
            if (resolved.matchNothing)
                return false;
            if (resolved.queryType && asset.type != *resolved.queryType)
                return false;
            if (!resolved.extToken.empty() && StringUtil::toLower(asset.extension) != resolved.extToken)
                return false;
            if (resolved.hasGuidToken && normalizePathForCompare(asset.path) != resolved.guidPath)
                return false;
            if (resolved.hasRefToken && resolved.refMatchPaths.count(normalizePathForCompare(asset.path)) == 0)
                return false;
        }

        return true;
    }

    AssetClickResult AssetGridRenderer::draw(
        const std::vector<Asset>& assets,
        const AssetFilter& filter,
        const ResolvedAssetFilter& resolved)
    {
        AssetClickResult result;

        // Build filtered + sorted view
        std::vector<const Asset*> filtered;
        filtered.reserve(assets.size());
        for (const auto& asset : assets)
        {
            if (matchesFilter(asset, filter, resolved))
                filtered.push_back(&asset);
        }

        std::sort(filtered.begin(), filtered.end(), [&](const Asset* a, const Asset* b) {
            // Folders always first
            bool aDir = a->isDirectory;
            bool bDir = b->isDirectory;
            if (aDir != bDir) return aDir > bDir;

            int cmp = 0;
            switch (filter.sortBy)
            {
            case SortField::Date:
                cmp = (a->lastModified < b->lastModified) ? -1 : (a->lastModified > b->lastModified) ? 1 : 0;
                break;
            case SortField::Size:
                cmp = (a->fileSize < b->fileSize) ? -1 : (a->fileSize > b->fileSize) ? 1 : 0;
                break;
            case SortField::Type:
                cmp = a->extension.compare(b->extension);
                break;
            case SortField::Name:
            default:
            {
                std::string aLower = StringUtil::toLower(a->name);
                std::string bLower = StringUtil::toLower(b->name);
                cmp = aLower.compare(bLower);
                break;
            }
            }
            return filter.sortAscending ? (cmp < 0) : (cmp > 0);
        });

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

        for (const auto* asset : filtered)
        {
            drawAssetItem(*asset, asset->isSelected, selectedPaths, result);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                result.wasClicked = true;
                result.clickedPath = asset->path;
                result.clickedType = asset->type;

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                    && (asset->type != AssetType::Other || !asset->isDirectory))
                {
                    result.wasDoubleClicked = true;
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

        AtlasIcon icon;
        bool isFolder = false;

        if (asset.type == AssetType::Other && asset.isDirectory)
        {
            icon = AtlasIcon::Folder;
            isFolder = true;
        }
        else
        {
            icon = assetTypeInfo(asset.type).icon;
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
