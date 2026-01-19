#pragma once
#include "data/EntityHandle.hpp"
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

        std::string newAnimatorName;
        bool showCreateAnimatorModal = false;

        std::string newVFXName;
        bool showCreateVFXModal = false;

        std::string newPrefabName;
        bool showSavePrefabModal = false;
        services::EntityHandle pendingSavePrefabEntity;

        std::string renameFileName;
        bool showRenameFileModal = false;

        bool showDeleteConfirmModal = false;
        
        bool showErrorModal = false;
        std::string errorTitle;
        std::string errorMessage;
        std::vector<std::string> errorDetails;

    public:
        using RefreshCallback = std::function<void()>;
        using ClipboardCallback = std::function<void()>;
        using PasteCallback = std::function<bool()>;  

        explicit ContentBrowserModals(RefreshCallback onRefresh);
        ~ContentBrowserModals() = default;
        
        void setClipboardCallbacks(ClipboardCallback onCut, ClipboardCallback onCopy,
                                   PasteCallback onPaste, std::function<bool()> hasClipboardItems);

        void processModals(const fs::path& currentPath, const fs::path& selectedFile);
        void drawContextMenu(const fs::path& selectedFile);
        void triggerSavePrefabModal(const services::EntityHandle& entity);
        void triggerDeleteModal();

        void showError(const std::string& title, const std::string& message,
                       const std::vector<std::string>& details = {});

    private:
        void drawCreateFolderModal(const fs::path& currentPath);
        void drawCreateMaterialModal(const fs::path& currentPath);
        void drawCreateAnimatorModal(const fs::path& currentPath);
        void drawCreateVFXModal(const fs::path& currentPath);
        void drawSavePrefabModal(const fs::path& currentPath);
        void drawRenameModal(const fs::path& selectedFile);
        void drawDeleteModal(const fs::path& selectedFile);
        void drawErrorModal();

        void createFolder(const fs::path& currentPath, const std::string& name);

        RefreshCallback refreshCallback;
        ClipboardCallback cutCallback;
        ClipboardCallback copyCallback;
        PasteCallback pasteCallback;
        std::function<bool()> hasClipboardItemsCallback;
    };
}
