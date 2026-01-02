#pragma once
#include "data/EntityHandle.hpp"
#include <filesystem>
#include <string>
#include <functional>

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

        std::string newPrefabName;
        bool showSavePrefabModal = false;
        services::EntityHandle pendingSavePrefabEntity;

        std::string renameFileName;
        bool showRenameFileModal = false;

        bool showDeleteConfirmModal = false;
    public:
        using RefreshCallback = std::function<void()>;

        explicit ContentBrowserModals(RefreshCallback onRefresh);
        ~ContentBrowserModals() = default;

        void processModals(const fs::path& currentPath, const fs::path& selectedFile);
        void drawContextMenu(const fs::path& selectedFile);
        void triggerSavePrefabModal(const services::EntityHandle& entity);

    private:
        void drawCreateFolderModal(const fs::path& currentPath);
        void drawCreateMaterialModal(const fs::path& currentPath);
        void drawSavePrefabModal(const fs::path& currentPath);
        void drawRenameModal(const fs::path& selectedFile);
        void drawDeleteModal(const fs::path& selectedFile);

        void createFolder(const fs::path& currentPath, const std::string& name);
        
        RefreshCallback refreshCallback;
    };
}
