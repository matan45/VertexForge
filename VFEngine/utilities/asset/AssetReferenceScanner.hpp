#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace asset
{
    namespace fs = std::filesystem;

    // Result of a reference scan/update operation
    struct ReferenceScanResult
    {
        std::vector<std::string> referencingFiles;  // Files that contain references to the target
        std::vector<std::string> updatedFiles;      // Files that were modified (for update operations)
        std::vector<std::string> failedFiles;       // Files that failed to update
    };

    // Utility class for scanning and updating asset path references in project files
    class AssetReferenceScanner
    {
    public:
        // Find all files that reference a given asset path
        // Scans .vfScene, .vfPrefab, .vfMat, .vfMatInstance files
        static ReferenceScanResult findReferencingFiles(
            const std::string& assetPath,
            const std::string& searchRoot
        );

        // Update all references from oldPath to newPath in project files
        // Returns information about which files were modified
        static ReferenceScanResult updateReferences(
            const std::string& oldPath,
            const std::string& newPath,
            const std::string& searchRoot
        );

        // Check if a specific file contains a reference to the given path
        static bool fileContainsReference(
            const std::string& filePath,
            const std::string& targetPath
        );

        // Get the original content of referencing files (for undo)
        // Returns map of filepath -> original file content
        static std::vector<std::pair<std::string, std::string>> getOriginalContents(
            const std::vector<std::string>& referencingFiles
        );

        // Restore files from original content (for undo)
        static bool restoreOriginalContents(
            const std::vector<std::pair<std::string, std::string>>& originalContents
        );

    private:
        // Scan specific file types
        static bool scanSceneOrPrefabFile(const fs::path& filePath, const std::string& targetPath);
        static bool scanMaterialFile(const fs::path& filePath, const std::string& targetPath);
        static bool scanMaterialInstanceFile(const fs::path& filePath, const std::string& targetPath);

        // Update specific file types
        static bool updateSceneOrPrefabFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);
        static bool updateMaterialFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);
        static bool updateMaterialInstanceFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);

        // Helper to normalize path separators for comparison
        static std::string normalizePath(const std::string& path);

        // Helper to recursively scan directory for asset files
        static std::vector<fs::path> collectAssetFiles(const fs::path& searchRoot);
    };
}
