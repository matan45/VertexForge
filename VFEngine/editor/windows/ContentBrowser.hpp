#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "imgui.h"
#include "data/DTOs.hpp"

#include <string>
#include <filesystem>
#include <unordered_map>
#include <memory>
namespace fs = std::filesystem;

namespace windows
{
    // Forward declarations
    class MeshPreviewWindow;
    class ImagePreviewWindow;
    class AudioPreviewWindow;

    enum class AssetType { Texture, HDR, Model, Audio, Animation, Shader, Scene, Other };

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
        services::EditorTextureHandle sceneIcon;

        // Service-based image preview handle
        services::EditorTextureHandle selectedImageHandle;

        // Pending release handle - to defer release to next frame
        services::EditorTextureHandle pendingReleaseHandle;

        bool navigateFolder = false;
        bool iconsLoaded = false;
        bool importLocationSet = false;

        // Track open preview windows (key = file path)
        std::unordered_map<std::string, std::weak_ptr<MeshPreviewWindow>> openMeshPreviews;
        std::unordered_map<std::string, std::weak_ptr<ImagePreviewWindow>> openImagePreviews;
        std::unordered_map<std::string, std::weak_ptr<AudioPreviewWindow>> openAudioPreviews;

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

        void printFilesNames(const Asset& asset, bool isSelected);

        void drawFileWindow();

        void createNewFolder(const std::string& folderName);
        void createNewFolderModel();
        void handleCreateFiles();

        void drawFolderTree(const fs::path& path);
        bool matchesSearchQuery(const Asset& asset) const;
        void deferredRelease();
    };
}
