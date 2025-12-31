#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "imgui.h"
#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventTypes.hpp"

#include <string>
#include <filesystem>
#include <unordered_map>
#include <memory>
namespace fs = std::filesystem;

namespace windows
{
    class MeshPreviewWindow;
    class ImagePreviewWindow;
    class AudioPreviewWindow;
    class MaterialEditorWindow;
    class PrefabPreviewWindow;

    enum class AssetType { Texture, HDR, Model, Audio, Animation, Shader, Scene, Material, Prefab, Other };
    
    enum class AtlasIcon : uint32_t
    {
        Animation = 0,
        Texture = 1,    // image
        Glsl = 2,
        Mesh = 3,
        Material = 4,
        Folder = 5,
        Scene = 6,
        Hdr = 7,
        Audio = 8,
        File = 9,
        Prefab = 10
    };

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

        std::string newMaterialName;
        bool showCreateMaterialModal = false;

        std::string renameFileName;
        bool showRenameFileModal = false;
        bool showDeleteConfirmModal = false;

        // Prefab save modal
        std::string newPrefabName;
        bool showSavePrefabModal = false;
        services::EntityHandle pendingSavePrefabEntity;

        fs::path selectedFile;
        AssetType selectedType;
        bool showFileWindow = false;
        
        services::EditorTextureHandle iconAtlas;
        static constexpr uint32_t ATLAS_GRID_SIZE = 4;

        std::string pendingNavigation;  // Deferred navigation to avoid iterator invalidation
        bool iconsLoaded = false;
        bool importLocationSet = false;

        // Event subscription for auto-refresh after import
        events::SubscriptionToken importCompletedToken;

        // Track open preview windows (key = file path)
        std::unordered_map<std::string, std::weak_ptr<MeshPreviewWindow>> openMeshPreviews;
        std::unordered_map<std::string, std::weak_ptr<ImagePreviewWindow>> openImagePreviews;
        std::unordered_map<std::string, std::weak_ptr<AudioPreviewWindow>> openAudioPreviews;
        std::unordered_map<std::string, std::weak_ptr<MaterialEditorWindow>> openMaterialEditors;
        std::unordered_map<std::string, std::weak_ptr<PrefabPreviewWindow>> openPrefabPreviews;

        static constexpr float THUMBNAIL_SIZE = 64.0f;
        static constexpr float PADDING = 16.0f;

    public:
        ContentBrowser();
        ~ContentBrowser() override;

        void draw() override;

    private:
        void loadIconAtlas();
        void navigateTo(const fs::path& path);
        
        static std::pair<ImVec2, ImVec2> getAtlasUV(AtlasIcon icon);

        void loadDirectory(const fs::path& path);

        void printFilesNames(const Asset& asset, bool isSelected);

        void drawFileWindow();

        void createNewFolder(const std::string& folderName);
        void createNewFolderModel();
        void createNewMaterialModal();
        void savePrefabModal();
        void renameFileModal();
        void deleteFileConfirmModal();
        void handleCreateFiles();

        void drawFolderTree(const fs::path& path);
        bool matchesSearchQuery(const Asset& asset) const;

        void handleModals();
        void drawFolderStructurePanel();
        void drawContentPanel();
        void drawToolbar();
        void drawAssetGrid();
    };
}
