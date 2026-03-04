#include "AssetReferenceScanner.hpp"
#include "../terrain/TerrainSerializer.hpp"
#include "../threading/JobSystem.hpp"
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

            if (ext == ".vfscene" || ext == ".vfprefab" || ext == ".vfmat" || ext == ".vfmatinstance"
                || ext == ".vfanimator" || ext == ".vfvfx" || ext == ".vfterrainmat" || ext == ".vfterrain")
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

        std::vector<std::future<bool>> futures;
        futures.reserve(files.size());

        for (const auto& file : files)
        {
            std::string ext = file.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            futures.push_back(threading::JobSystem::instance().submit(
                [file, &assetPath, ext]() -> bool
                {
                    if (ext == ".vfterrain")
                    {
                        return scanTerrainFile(file, assetPath);
                    }
                    return scanSceneOrPrefabFile(file, assetPath);
                }, threading::JobPriority::NORMAL
            ));
        }

        for (size_t i = 0; i < futures.size(); ++i)
        {
            if (futures[i].get())
            {
                result.referencingFiles.push_back(files[i].string());
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

    bool AssetReferenceScanner::scanTerrainFile(const fs::path& filePath, const std::string& targetPath)
    {
        try
        {
            terrain::TerrainFileHeader header;
            std::vector<terrain::TileIndexEntry> index;
            if (!terrain::TerrainSerializer::readHeader(filePath.string(), header, index))
            {
                return false;
            }

            std::string normalizedTarget = normalizePath(targetPath);
            std::string normalizedMaterial = normalizePath(header.materialPath);

            return header.materialPath == targetPath ||
                   normalizedMaterial == normalizedTarget;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool AssetReferenceScanner::updateTerrainFile(const fs::path& filePath, const std::string& oldPath,
                                                   const std::string& newPath)
    {
        try
        {
            terrain::TerrainFileHeader header;
            std::vector<terrain::TileIndexEntry> index;
            if (!terrain::TerrainSerializer::readHeader(filePath.string(), header, index))
            {
                return false;
            }

            std::string oldPathNorm = normalizePath(oldPath);
            std::string materialNorm = normalizePath(header.materialPath);

            if (header.materialPath != oldPath && materialNorm != oldPathNorm)
            {
                return false;
            }

            // Read the entire file into memory
            std::ifstream inFile(filePath, std::ios::binary | std::ios::ate);
            if (!inFile.is_open())
            {
                return false;
            }
            auto fileSize = inFile.tellg();
            inFile.seekg(0);
            std::vector<char> fileData(static_cast<size_t>(fileSize));
            inFile.read(fileData.data(), fileSize);
            inFile.close();

            // Calculate the byte offset to the materialPath length field
            // Header layout: magic(4) + versions(12) + flags(4) + tileCount(4) + resolution(1)
            //   + worldTileSize(4) + maxHeight(4) + minHeight(4) + skirtDepth(4)
            //   + lodDistances(16) + gridBounds(16) = 73 bytes, then pathLen(4) + path(pathLen)
            size_t pathLenOffset = 4 + 12 + 4 + 4 + 1 + 16 + 16 + 16; // 73
            uint32_t oldPathLen = static_cast<uint32_t>(header.materialPath.size());
            int32_t delta = static_cast<int32_t>(newPath.size()) - static_cast<int32_t>(oldPathLen);

            // Build the new file: [header before pathLen][new pathLen][new path][rest of file after old path]
            size_t oldPathEnd = pathLenOffset + 4 + oldPathLen;

            std::vector<char> newFileData;
            newFileData.reserve(fileData.size() + delta);

            // Copy everything before pathLen
            newFileData.insert(newFileData.end(), fileData.begin(), fileData.begin() + pathLenOffset);

            // Write new pathLen (little-endian)
            uint32_t newPathLen = static_cast<uint32_t>(newPath.size());
            newFileData.push_back(static_cast<char>(newPathLen & 0xFF));
            newFileData.push_back(static_cast<char>((newPathLen >> 8) & 0xFF));
            newFileData.push_back(static_cast<char>((newPathLen >> 16) & 0xFF));
            newFileData.push_back(static_cast<char>((newPathLen >> 24) & 0xFF));

            // Write new path
            newFileData.insert(newFileData.end(), newPath.begin(), newPath.end());

            // Copy the rest of the file after the old path
            newFileData.insert(newFileData.end(), fileData.begin() + oldPathEnd, fileData.end());

            // Adjust absolute offsets in the index table if delta != 0
            if (delta != 0 && !index.empty())
            {
                // Calculate where the physics config ends (if present)
                size_t physicsSize = 0;
                if (terrain::hasFlag(header.flags, terrain::TerrainFormatFlags::HAS_PHYSICS_DATA))
                {
                    physicsSize = 1 + 1 + 4 + 4; // hasCollider(1) + collisionLayer(1) + friction(4) + restitution(4)
                }

                // Index table starts right after header (pathLen + path + optional physics)
                size_t indexTableOffset = pathLenOffset + 4 + newPathLen + physicsSize;

                // Each index entry is 44 bytes: coordX(4) + coordZ(4) + heightDataOffset(8) + heightDataSize(4)
                //   + weightDataOffset(8) + meshletDataOffset(8) + holeMaskDataOffset(8)
                for (size_t i = 0; i < index.size(); ++i)
                {
                    size_t entryOffset = indexTableOffset + i * 44;

                    // Adjust heightDataOffset at byte 8 of the entry
                    auto adjustOffset = [&](size_t fieldByteOffset, uint64_t originalValue) {
                        if (originalValue == 0) return;
                        uint64_t adjusted = static_cast<uint64_t>(static_cast<int64_t>(originalValue) + delta);
                        size_t pos = entryOffset + fieldByteOffset;
                        for (int b = 0; b < 8; ++b)
                        {
                            newFileData[pos + b] = static_cast<char>((adjusted >> (b * 8)) & 0xFF);
                        }
                    };

                    // heightDataOffset is at offset 8 in the entry (after coordX(4) + coordZ(4))
                    adjustOffset(8, index[i].heightDataOffset);
                    // weightDataOffset is at offset 20 (8 + 8 + 4)
                    adjustOffset(20, index[i].weightDataOffset);
                    // meshletDataOffset is at offset 28 (20 + 8)
                    adjustOffset(28, index[i].meshletDataOffset);
                    // holeMaskDataOffset is at offset 36 (28 + 8)
                    adjustOffset(36, index[i].holeMaskDataOffset);
                }
            }

            // Write the modified file
            std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
            if (!outFile.is_open())
            {
                vfLogError("Failed to open terrain file for writing: {}", filePath.string());
                return false;
            }
            outFile.write(newFileData.data(), static_cast<std::streamsize>(newFileData.size()));
            outFile.close();

            vfLogInfo("Updated terrain material path in: {}", filePath.string());
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to update terrain file {}: {}", filePath.string(), e.what());
            return false;
        }
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
            else if (ext == ".vfanimator" || ext == ".vfvfx" || ext == ".vfterrainmat")
            {
                success = updateSceneOrPrefabFile(filePath, oldPath, newPath);
            }
            else if (ext == ".vfterrain")
            {
                success = updateTerrainFile(filePath, oldPath, newPath);
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
