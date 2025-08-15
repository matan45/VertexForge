#include "FileUtils.hpp"
#include "string/StringUtil.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace files
{
    std::string FileUtils::getFileExtension(std::string_view filePath, bool lowerCase)
    {
        fs::path fsPath(filePath.data());
        std::string extension = fsPath.extension().string();
        extension.erase(std::ranges::find(extension.begin(), extension.end(), '\0'), extension.end());
        if (lowerCase)
        {
            extension = StringUtil::toLower(extension);
        }
        return extension;
    }

    std::string FileUtils::getFileName(std::string_view filePath)
    {
        fs::path fsPath(filePath.data());
        // Get the file name
        return fsPath.stem().string();
    }

    // UI helper methods - simple extension-based checking
    bool FileUtils::isHDRFile(std::string_view filePath)
    {
        std::string extension = getFileExtension(filePath);
        return extension == ".hdr" || extension == ".exr";
    }

    bool FileUtils::isTextureFile(std::string_view filePath)
    {
        std::string extension = getFileExtension(filePath);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp";
    }

    bool FileUtils::isMeshFile(std::string_view filePath)
    {
        std::string extension = getFileExtension(filePath);
        return extension == ".obj" || extension == ".fbx" || extension == ".dae" || extension == ".gltf" || extension == ".glb";
    }

    bool FileUtils::isAudioFile(std::string_view filePath)
    {
        std::string extension = getFileExtension(filePath);
        return extension == ".mp3" || extension == ".wav" || extension == ".ogg";
    }
}
