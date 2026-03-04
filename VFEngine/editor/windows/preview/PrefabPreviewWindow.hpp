#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <future>
#include <atomic>
#include <glm/glm.hpp>

namespace windows
{
    // Represents a single node in the prefab entity tree (for display only)
    struct PrefabEntityNode
    {
        std::string name;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        std::vector<std::string> componentTypes;
        std::vector<PrefabEntityNode> children;

        // Asset paths
        std::string meshPath;
        std::string materialPath;
        std::string audioPath;
    };
    
    struct ComponentStats
    {
        std::map<std::string, uint32_t> counts;
        uint32_t totalEntities = 0;
    };
    
    struct PrefabLoadResult
    {
        bool success = false;
        std::string errorMessage;
        std::string prefabName;
        std::string version;
        PrefabEntityNode rootEntity;
        ComponentStats stats;
    };

    class PrefabPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string prefabPath;
        std::string windowTitle;

        // Loaded prefab data
        std::string prefabName;
        std::string prefabVersion;
        PrefabEntityNode rootEntity;
        ComponentStats componentStats;
        std::string errorMessage;
        bool loadFailed = false;
        bool prefabLoaded = false;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        // Async loading state
        std::future<PrefabLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        std::string loadingStatus = "Loading prefab...";

        // Tree view state
        std::optional<std::string> selectedEntityPath;

    public:
        explicit PrefabPreviewWindow(const std::string& filePath);
        ~PrefabPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getPrefabPath() const { return prefabPath; }

    private:
        void startAsyncLoad();
        void updateAsyncLoading();
        PrefabLoadResult loadPrefabBackground(const std::string& path);

        void drawInfoPanel();
        void drawEntityTreePanel();
        void drawEntityNode(const PrefabEntityNode& node, const std::string& path);
        void drawLoadingIndicator();

        const PrefabEntityNode* findNodeByPath(const std::string& path) const;
        const PrefabEntityNode* findNodeByPathRecursive(
            const PrefabEntityNode& node,
            const std::string& currentPath,
            const std::string& targetPath) const;
    };
}
