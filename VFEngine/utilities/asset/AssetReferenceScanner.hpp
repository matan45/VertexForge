#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace asset
{
    namespace fs = std::filesystem;
    
    struct ReferenceScanResult
    {
        std::vector<std::string> referencingFiles;  
        std::vector<std::string> updatedFiles;      
        std::vector<std::string> failedFiles;      
    };

    
    class AssetReferenceScanner
    {
    public:
        // Find all files that reference a given asset path
        // Scans .vfScene, .vfPrefab, .vfMat, .vfMatInstance files
        static ReferenceScanResult findReferencingFiles(
            const std::string& assetPath,
            const std::string& searchRoot
        );
        
        static ReferenceScanResult updateReferences(
            const std::string& oldPath,
            const std::string& newPath,
            const std::string& searchRoot
        );

        static std::vector<std::pair<std::string, std::string>> getOriginalContents(
            const std::vector<std::string>& referencingFiles
        );
        
        static bool restoreOriginalContents(
            const std::vector<std::pair<std::string, std::string>>& originalContents
        );

    private:
      
        static bool scanSceneOrPrefabFile(const fs::path& filePath, const std::string& targetPath);
        static bool scanMaterialFile(const fs::path& filePath, const std::string& targetPath);
        static bool scanMaterialInstanceFile(const fs::path& filePath, const std::string& targetPath);
        
        static bool updateSceneOrPrefabFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);
        static bool updateMaterialFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);
        static bool updateMaterialInstanceFile(const fs::path& filePath, const std::string& oldPath, const std::string& newPath);
        
        static std::string normalizePath(const std::string& path);

        static std::vector<fs::path> collectAssetFiles(const fs::path& searchRoot);

        static bool updateJsonStrings(nlohmann::json& j,
            const std::string& oldPath, const std::string& newPath,
            const std::string& oldPathNorm, const std::string& newPathNorm,
            const std::string& oldPathBackslash, const std::string& newPathBackslash);
    };
}
