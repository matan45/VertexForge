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
    private:
        services::EditorTextureHandle iconAtlas;
        bool iconsLoaded = false;

        static constexpr uint32_t ATLAS_COLS = 4;
        static constexpr uint32_t ATLAS_ROWS = 5;
        static constexpr float THUMBNAIL_SIZE = 64.0f;
        static constexpr float PADDING = 16.0f;
    public:
        explicit AssetGridRenderer() = default;
        ~AssetGridRenderer() = default;

        void ensureIconsLoaded();

        AssetClickResult draw(
            const std::vector<Asset>& assets,
            const std::string& searchQuery
        );

    private:
        void loadIconAtlas();
        static std::pair<ImVec2, ImVec2> getAtlasUV(AtlasIcon icon);
        void drawAssetItem(const Asset& asset, bool isSelected, const std::vector<std::string>& selectedPaths, AssetClickResult& result);
        static bool matchesSearchQuery(const Asset& asset, const std::string& searchQuery);
    };
}
