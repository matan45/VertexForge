#pragma once
#include "ContentBrowserTypes.hpp"
#include "data/DTOs.hpp"
#include <imgui.h>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    struct AssetClickResult
    {
        bool wasClicked = false;
        bool wasDoubleClicked = false;
        fs::path clickedPath;
        AssetType clickedType = AssetType::Other;
        std::string pendingNavigation;
    };

    class AssetGridRenderer
    {
    public:
        AssetGridRenderer() = default;
        ~AssetGridRenderer() = default;

        void ensureIconsLoaded();

        AssetClickResult draw(
            const std::vector<Asset>& assets,
            const fs::path& selectedFile,
            const std::string& searchQuery
        );

        bool isIconAtlasValid() const { return iconAtlas.isValid(); }

    private:
        void loadIconAtlas();
        static std::pair<ImVec2, ImVec2> getAtlasUV(AtlasIcon icon);
        void drawAssetItem(const Asset& asset, bool isSelected, AssetClickResult& result);
        static bool matchesSearchQuery(const Asset& asset, const std::string& searchQuery);

        services::EditorTextureHandle iconAtlas;
        bool iconsLoaded = false;

        static constexpr uint32_t ATLAS_GRID_SIZE = 4;
        static constexpr float THUMBNAIL_SIZE = 64.0f;
        static constexpr float PADDING = 16.0f;
    };
}
