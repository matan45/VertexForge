#pragma once
#include "../data/DTOs.hpp"
#include <vector>
#include <string>

namespace services {

    // Resource types
    enum class ResourceType {
        Unknown,
        Mesh,
        Texture,
        HDRTexture,
        Audio,
        Animation,
        Scene
    };

    // Resource service interface - abstracts resource/import operations
    // Presentation layer uses this instead of direct Import controller access
    class IResourceService {
    public:
        virtual ~IResourceService() = default;

        // ============================================
        // Import Operations
        // ============================================

        // Import files (async operation)
        virtual void importFiles(const std::vector<ImportFileRequest>& files) = 0;

        // Set the output location for imported assets
        virtual void setImportLocation(const std::string& path) = 0;

        // Get the current import location
        virtual std::string getImportLocation() const = 0;

        // Cancel any pending import operations
        virtual void cancelImport() = 0;

        // ============================================
        // Import State
        // ============================================

        // Check if import is currently in progress
        virtual bool isImporting() const = 0;

        // Get import progress (0.0 - 1.0)
        virtual float getImportProgress() const = 0;

        // Get the current file being imported
        virtual std::string getCurrentImportFile() const = 0;

        // ============================================
        // Resource Queries
        // ============================================

        // Check if a resource is loaded
        virtual bool isResourceLoaded(const std::string& path) const = 0;

        // Get the type of a resource based on path/extension
        virtual ResourceType getResourceType(const std::string& path) const = 0;

        // ============================================
        // File System Helpers
        // ============================================

        // Check if path exists
        virtual bool pathExists(const std::string& path) const = 0;

        // Check if path is a directory
        virtual bool isDirectory(const std::string& path) const = 0;

        // Get supported file extensions for import
        virtual std::vector<std::string> getSupportedExtensions() const = 0;

        // Check if a file extension is supported for import
        virtual bool isExtensionSupported(const std::string& extension) const = 0;
    };

}
