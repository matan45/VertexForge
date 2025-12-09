#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "imgui.h"
#include "data/DTOs.hpp"

#include <string>
#include <filesystem>
namespace fs = std::filesystem;

namespace windows
{
    enum class AssetType { Texture, HDR, Model, Audio, Animation, Shader, Other };

    struct Asset
    {
        std::string name;
        std::string path;
        AssetType type;
    };


    class ContentBrowser : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::vector<Asset> assets;
        fs::path currentPath = "C:\\matan"; //todo get this path from the project file
        std::string searchQuery;

        std::string newFolderName;
        bool showCreateFolderModal = false;

        bool isShaderLoaded = false;

        fs::path selectedFile;
        AssetType selectedType;
        bool showFileWindow = false;

        // Service-based icons
        services::EditorTextureHandle fileIcon;
        services::EditorTextureHandle folderIcon;
        services::EditorTextureHandle textureIcon;
        services::EditorTextureHandle audioIcon;
        services::EditorTextureHandle meshIcon;
        services::EditorTextureHandle glslIcon;
        services::EditorTextureHandle animationIcon;
        services::EditorTextureHandle hdrIcon;

        // Service-based image preview handle
        services::EditorTextureHandle selectedImageHandle;

        // Pending release handle - to defer release to next frame
        services::EditorTextureHandle pendingReleaseHandle;

        bool navigateFolder = false;
        bool iconsLoaded = false;
        bool importLocationSet = false;

        static constexpr float THUMBNAIL_SIZE = 64.0f;
        static constexpr float PADDING = 16.0f;

    public:
        ContentBrowser();
        ~ContentBrowser() override = default;

        void draw() override;

    private:
        void loadIcons();
        void navigateTo(const fs::path& path);

        void loadDirectory(const fs::path& path);

        void printFilesNames(const Asset& asset);

        void drawFileWindow();

        void createNewFolder(const std::string& folderName);
        void createNewFolderModel();
        void handleCreateFiles();

        void drawFolderTree(const fs::path& path);
        bool matchesSearchQuery(const Asset& asset) const;
    };
}
