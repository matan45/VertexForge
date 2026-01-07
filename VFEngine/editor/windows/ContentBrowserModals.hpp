#pragma once
#include "data/EntityHandle.hpp"
#include "data/FileOperationsTypes.hpp"
#include <filesystem>
#include <string>
#include <functional>
#include <vector>

namespace fs = std::filesystem;

namespace windows
{
    // Conflict resolution options
    enum class ConflictResolution
    {
        None,
        Skip,
        Rename,
        Overwrite
    };

    class ContentBrowserModals
    {
    private:
        std::string newFolderName;
        bool showCreateFolderModal = false;

        std::string newMaterialName;
        bool showCreateMaterialModal = false;

        std::string newPrefabName;
        bool showSavePrefabModal = false;
        services::EntityHandle pendingSavePrefabEntity;

        std::string renameFileName;
        bool showRenameFileModal = false;

        bool showDeleteConfirmModal = false;

        // Error modal state
        bool showErrorModal = false;
        std::string errorTitle;
        std::string errorMessage;
        std::vector<std::string> errorDetails;

        // Conflict resolution modal state
        bool showConflictModal = false;
        std::string conflictSourcePath;
        std::string conflictDestPath;
        std::string conflictNewName;
        std::function<void(ConflictResolution, const std::string&)> conflictCallback;

    public:
        using RefreshCallback = std::function<void()>;
        using ClipboardCallback = std::function<void()>;
        using PasteCallback = std::function<bool()>;  // Returns true if paste succeeded

        explicit ContentBrowserModals(RefreshCallback onRefresh);
        ~ContentBrowserModals() = default;

        // Set clipboard callbacks for context menu
        void setClipboardCallbacks(ClipboardCallback onCut, ClipboardCallback onCopy,
                                   PasteCallback onPaste, std::function<bool()> hasClipboardItems);

        void processModals(const fs::path& currentPath, const fs::path& selectedFile);
        void drawContextMenu(const fs::path& selectedFile);
        void triggerSavePrefabModal(const services::EntityHandle& entity);
        void triggerDeleteModal();

        // Error and conflict modals
        void showError(const std::string& title, const std::string& message,
                       const std::vector<std::string>& details = {});
        void showOperationError(const services::FileOperationResult& result);
        void showConflict(const std::string& sourcePath, const std::string& destPath,
                          std::function<void(ConflictResolution, const std::string&)> callback);

    private:
        void drawCreateFolderModal(const fs::path& currentPath);
        void drawCreateMaterialModal(const fs::path& currentPath);
        void drawSavePrefabModal(const fs::path& currentPath);
        void drawRenameModal(const fs::path& selectedFile);
        void drawDeleteModal(const fs::path& selectedFile);
        void drawErrorModal();
        void drawConflictModal();

        void createFolder(const fs::path& currentPath, const std::string& name);

        RefreshCallback refreshCallback;
        ClipboardCallback cutCallback;
        ClipboardCallback copyCallback;
        PasteCallback pasteCallback;
        std::function<bool()> hasClipboardItemsCallback;
    };
}
