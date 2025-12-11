#pragma once
#include "../interfaces/IResourceService.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/ResourceEvents.hpp"
#include <atomic>
#include <string>
#include <functional>
#include <mutex>
#include <future>

namespace services {

    // Progress callback type for import operations
    using ImportProgressCallback = std::function<void(
        std::string_view currentFile,
        uint32_t fileIndex,
        uint32_t totalFiles,
        float fileProgress
    )>;

    // Delegate types for import functionality (injected by Editor)
    struct ImportDelegate {
        std::function<void()> initialize;
        std::function<void(const std::vector<ImportFileRequest>&, ImportProgressCallback)> importFiles;
        std::function<void(const std::string&)> setLocation;
    };

    class ResourceServiceImpl : public IResourceService {
    public:
        ResourceServiceImpl();
        ~ResourceServiceImpl() override = default;

        // Register all command and query handlers with the EventDispatcher
        void registerEventHandlers();

        // Set the import delegate (called by Editor during initialization)
        void setImportDelegate(const ImportDelegate& delegate);

        // Import Operations
        void importFiles(const std::vector<ImportFileRequest>& files) override;
        void setImportLocation(const std::string& path) override;
        std::string getImportLocation() const override;
        void cancelImport() override;

        // Import State
        bool isImporting() const override;
        float getImportProgress() const override;
        std::string getCurrentImportFile() const override;

        // Resource Queries
        bool isResourceLoaded(const std::string& path) const override;
        ResourceType getResourceType(const std::string& path) const override;

        // File System Helpers
        bool pathExists(const std::string& path) const override;
        bool isDirectory(const std::string& path) const override;
        std::vector<std::string> getSupportedExtensions() const override;
        bool isExtensionSupported(const std::string& extension) const override;

    private:
        ImportDelegate importDelegate;
        std::string importLocation;
        std::atomic<bool> importing{ false };
        std::atomic<float> progress{ 0.0f };
        std::string currentFile;
        mutable std::mutex currentFileMutex;
        std::future<void> importFuture;  // For async import

        std::string getExtension(const std::string& path) const;
    };

}
