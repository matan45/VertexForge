#include "AssetReferenceScanner.hpp"
#include "../print/EditorLogger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace asset
{
    using json = nlohmann::json;

    std::string AssetReferenceScanner::normalizePath(const std::string& path)
    {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        while (!normalized.empty() && normalized.back() == '/')
        {
            normalized.pop_back();
        }
        return normalized;
    }

    std::vector<fs::path> AssetReferenceScanner::collectAssetFiles(const fs::path& searchRoot)
    {
        std::vector<fs::path> files;

        if (!fs::exists(searchRoot) || !fs::is_directory(searchRoot))
        {
            return files;
        }

        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(
                 searchRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            if (!entry.is_regular_file())
            {
                continue;
            }

            std::string ext = entry.path().extension().string();

            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".vfscene" || ext == ".vfprefab" || ext == ".vfmat" || ext == ".vfmatinstance")
            {
                files.push_back(entry.path());
            }
        }

        return files;
    }

    bool AssetReferenceScanner::scanSceneOrPrefabFile(const fs::path& filePath, const std::string& targetPath)
    {
        try
        {
            std::ifstream file(filePath);
            if (!file.is_open())
            {
                return false;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            file.close();

            std::string normalizedTarget = normalizePath(targetPath);

            std::string targetWithBackslash = targetPath;
            std::replace(targetWithBackslash.begin(), targetWithBackslash.end(), '/', '\\');


            return content.find(targetPath) != std::string::npos ||
                content.find(targetWithBackslash) != std::string::npos ||
                content.find(normalizedTarget) != std::string::npos;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool AssetReferenceScanner::scanMaterialFile(const fs::path& filePath, const std::string& targetPath)
    {
        return scanSceneOrPrefabFile(filePath, targetPath);
    }

    bool AssetReferenceScanner::scanMaterialInstanceFile(const fs::path& filePath, const std::string& targetPath)
    {
        return scanSceneOrPrefabFile(filePath, targetPath);
    }

    ReferenceScanResult AssetReferenceScanner::findReferencingFiles(
        const std::string& assetPath,
        const std::string& searchRoot)
    {
        ReferenceScanResult result;

        auto files = collectAssetFiles(fs::path(searchRoot));

        for (const auto& file : files)
        {
            if (scanSceneOrPrefabFile(file, assetPath))
            {
                result.referencingFiles.push_back(file.string());
            }
        }

        return result;
    }

    bool AssetReferenceScanner::updateJsonStrings(json& j, const std::string& oldPath, const std::string& newPath,
                                                  const std::string& oldPathNorm, const std::string& newPathNorm,
                                                  const std::string& oldPathBackslash,
                                                  const std::string& newPathBackslash)
    {
        bool modified = false;

        if (j.is_string())
        {
            std::string value = j.get<std::string>();
            std::string normalizedValue = normalizePath(value);

            if (value == oldPath || value == oldPathBackslash || normalizedValue == oldPathNorm)
            {
                if (value.find('\\') != std::string::npos)
                {
                    j = newPathBackslash;
                }
                else
                {
                    j = newPath;
                }
                modified = true;
            }
        }
        else if (j.is_object())
        {
            for (auto& [key, val] : j.items())
            {
                if (updateJsonStrings(val, oldPath, newPath, oldPathNorm, newPathNorm, oldPathBackslash,
                                      newPathBackslash))
                {
                    modified = true;
                }
            }
        }
        else if (j.is_array())
        {
            for (auto& item : j)
            {
                if (updateJsonStrings(item, oldPath, newPath, oldPathNorm, newPathNorm, oldPathBackslash,
                                      newPathBackslash))
                {
                    modified = true;
                }
            }
        }

        return modified;
    }

    bool AssetReferenceScanner::updateSceneOrPrefabFile(const fs::path& filePath, const std::string& oldPath,
                                                        const std::string& newPath)
    {
        try
        {
            std::ifstream inFile(filePath);
            if (!inFile.is_open())
            {
                vfLogError("Failed to open file for reading: {}", filePath.string());
                return false;
            }

            json j = json::parse(inFile);
            inFile.close();

            // Prepare path variants
            std::string oldPathNorm = normalizePath(oldPath);
            std::string newPathNorm = normalizePath(newPath);

            std::string oldPathBackslash = oldPath;
            std::replace(oldPathBackslash.begin(), oldPathBackslash.end(), '/', '\\');

            std::string newPathBackslash = newPath;
            std::replace(newPathBackslash.begin(), newPathBackslash.end(), '/', '\\');

            bool modified = updateJsonStrings(j, oldPath, newPath, oldPathNorm, newPathNorm, oldPathBackslash,
                                              newPathBackslash);

            if (modified)
            {
                std::ofstream outFile(filePath);
                if (!outFile.is_open())
                {
                    vfLogError("Failed to open file for writing: {}", filePath.string());
                    return false;
                }

                outFile << j.dump(2);
                outFile.close();

                vfLogInfo("Updated references in: {}", filePath.string());
                return true;
            }

            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to update file {}: {}", filePath.string(), e.what());
            return false;
        }
    }

    bool AssetReferenceScanner::updateMaterialFile(const fs::path& filePath, const std::string& oldPath,
                                                   const std::string& newPath)
    {
        return updateSceneOrPrefabFile(filePath, oldPath, newPath);
    }

    bool AssetReferenceScanner::updateMaterialInstanceFile(const fs::path& filePath, const std::string& oldPath,
                                                           const std::string& newPath)
    {
        return updateSceneOrPrefabFile(filePath, oldPath, newPath);
    }

    ReferenceScanResult AssetReferenceScanner::updateReferences(
        const std::string& oldPath,
        const std::string& newPath,
        const std::string& searchRoot)
    {
        ReferenceScanResult result;

        // First find all referencing files
        auto scanResult = findReferencingFiles(oldPath, searchRoot);
        result.referencingFiles = scanResult.referencingFiles;

        // Update each file
        for (const auto& file : result.referencingFiles)
        {
            fs::path filePath(file);
            std::string ext = filePath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            bool success = false;

            if (ext == ".vfscene" || ext == ".vfprefab")
            {
                success = updateSceneOrPrefabFile(filePath, oldPath, newPath);
            }
            else if (ext == ".vfmat")
            {
                success = updateMaterialFile(filePath, oldPath, newPath);
            }
            else if (ext == ".vfmatinstance")
            {
                success = updateMaterialInstanceFile(filePath, oldPath, newPath);
            }

            if (success)
            {
                result.updatedFiles.push_back(file);
            }
            else
            {
                result.failedFiles.push_back(file);
            }
        }

        return result;
    }

    std::vector<std::pair<std::string, std::string>> AssetReferenceScanner::getOriginalContents(
        const std::vector<std::string>& referencingFiles)
    {
        std::vector<std::pair<std::string, std::string>> contents;

        for (const auto& file : referencingFiles)
        {
            try
            {
                std::ifstream inFile(file);
                if (inFile.is_open())
                {
                    std::stringstream buffer;
                    buffer << inFile.rdbuf();
                    contents.emplace_back(file, buffer.str());
                }
            }
            catch (const std::exception& e)
            {
                vfLogWarning("Failed to read original content of {}: {}", file, e.what());
            }
        }

        return contents;
    }

    bool AssetReferenceScanner::restoreOriginalContents(
        const std::vector<std::pair<std::string, std::string>>& originalContents)
    {
        bool allSuccess = true;

        for (const auto& [file, content] : originalContents)
        {
            try
            {
                std::ofstream outFile(file);
                if (outFile.is_open())
                {
                    outFile << content;
                    outFile.close();
                    vfLogInfo("Restored original content: {}", file);
                }
                else
                {
                    vfLogError("Failed to restore file: {}", file);
                    allSuccess = false;
                }
            }
            catch (const std::exception& e)
            {
                vfLogError("Failed to restore {}: {}", file, e.what());
                allSuccess = false;
            }
        }

        return allSuccess;
    }
}
