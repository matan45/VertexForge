#pragma once
#include "ContentBrowserTypes.hpp"
#include "data/EntityHandle.hpp"
#include <asset/AssetGUID.hpp>
#include <filesystem>
#include <string>
#include <functional>
#include <vector>

namespace fs = std::filesystem;

namespace windows
{
    class ContentBrowserModals
    {
    private:
        std::string newFolderName;
        bool showCreateFolderModal = false;

        std::string newMaterialName;
        bool showCreateMaterialModal = false;

        std::string newToonProfileName; // VK-1493
        bool showCreateToonProfileModal = false;

        std::string newAnimatorName;
        bool showCreateAnimatorModal = false;

        std::string newVFXName;
        bool showCreateVFXModal = false;

        std::string newVFXSequenceName;
        bool showCreateVFXSequenceModal = false;

        std::string newTerrainMaterialName;
        bool showCreateTerrainMaterialModal = false;

        std::string newBehaviorTreeName;
        bool showCreateBehaviorTreeModal = false;

        std::string newThemeName;
        bool showCreateThemeModal = false;

        // VK-1449: one generic modal serves every plugin-registered asset type.
        // The pending fields are set from the registry record when the user picks
        // the type from the Create menu.
        std::string newPluginAssetName;
        bool showCreatePluginAssetModal = false;
        std::string pendingPluginAssetExt;       // ".vfability" (already lowercased)
        std::string pendingPluginAssetTemplate;  // defaultTemplate JSON, or empty -> "{}"
        std::string pendingPluginAssetLabel;     // displayName, for the modal prompt

        std::string newPrefabName;
        bool showSavePrefabModal = false;
        services::EntityHandle pendingSavePrefabEntity;

        std::string renameFileName;
        bool showRenameFileModal = false;

        bool showDeleteConfirmModal = false;
        bool deleteDependentsChecked = false;
        std::vector<asset::AssetGUID> deleteDependents;
        std::vector<std::string> deleteTargets;

        bool showReferencesModal = false;
        asset::AssetGUID referencesGuid;
        std::string referencesAssetPath;

        bool showDependenciesModal = false;
        asset::AssetGUID dependenciesGuid;
        std::string dependenciesAssetPath;

        bool showErrorModal = false;
        std::string errorTitle;
        std::string errorMessage;
        std::vector<std::string> errorDetails;

        bool showResultModal = false;
        std::string resultTitle;
        std::string resultMessage;
        std::vector<std::string> resultDetails;

    public:
        using RefreshCallback = std::function<void()>;
        using ClipboardCallback = std::function<void()>;
        using PasteCallback = std::function<bool()>;  

        explicit ContentBrowserModals(RefreshCallback onRefresh);
        ~ContentBrowserModals() = default;
        
        void setClipboardCallbacks(ClipboardCallback onCut, ClipboardCallback onCopy,
                                   PasteCallback onPaste, std::function<bool()> hasClipboardItems);

        // Supplies the full current selection so Delete operates on every
        // selected item, not just the focused one.
        void setSelectionProvider(std::function<std::vector<std::string>()> provider);

        void processModals(const fs::path& currentPath, const fs::path& selectedFile);
        void drawContextMenu(const Asset* selectedAsset);
        void triggerSavePrefabModal(const services::EntityHandle& entity);
        void triggerDeleteModal();

        void showError(const std::string& title, const std::string& message,
                       const std::vector<std::string>& details = {});
        void showResult(const std::string& title, const std::string& message,
                        const std::vector<std::string>& details = {});

    private:
        void drawCreateFolderModal(const fs::path& currentPath);
        void drawCreateMaterialModal(const fs::path& currentPath);
        void drawCreateToonProfileModal(const fs::path& currentPath); // VK-1493
        void drawCreateAnimatorModal(const fs::path& currentPath);
        void drawCreateVFXModal(const fs::path& currentPath);
        void drawCreateVFXSequenceModal(const fs::path& currentPath);
        void drawCreateTerrainMaterialModal(const fs::path& currentPath);
        void drawCreateBehaviorTreeModal(const fs::path& currentPath);
        void drawCreateThemeModal(const fs::path& currentPath);
        void drawCreatePluginAssetModal(const fs::path& currentPath);
        void drawSavePrefabModal(const fs::path& currentPath);
        void drawRenameModal(const fs::path& selectedFile);
        void drawDeleteModal();
        void drawReferencesModal();
        void drawDependenciesModal();
        void drawErrorModal();
        void drawResultModal();

        // Lists asset paths for the given GUIDs (unresolved GUIDs shown
        // explicitly); returns true when a double-click navigated the
        // content browser so the caller can close its modal
        bool drawAssetGuidList(const std::vector<asset::AssetGUID>& guids);

        void createFolder(const fs::path& currentPath, const std::string& name);

        RefreshCallback refreshCallback;
        ClipboardCallback cutCallback;
        ClipboardCallback copyCallback;
        PasteCallback pasteCallback;
        std::function<bool()> hasClipboardItemsCallback;
        std::function<std::vector<std::string>()> selectionProvider;
    };
}
