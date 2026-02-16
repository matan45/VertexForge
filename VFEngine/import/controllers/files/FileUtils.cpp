#include "FileUtils.hpp"
#include "string/StringUtil.hpp"
#include <filesystem>
#include <initializer_list>

namespace fs = std::filesystem;

namespace files
{
    namespace
    {
        bool hasExtension(std::string_view filePath, std::initializer_list<const char*> extensions)
        {
            std::string ext = FileUtils::getFileExtension(filePath);
            for (const char* e : extensions)
            {
                if (ext == e)
                    return true;
            }
            return false;
        }
    }

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
        return fsPath.stem().string();
    }

    bool FileUtils::isHDRFile(std::string_view filePath)
    {
        return hasExtension(filePath, {".hdr", ".exr"});
    }

    bool FileUtils::isTextureFile(std::string_view filePath)
    {
        return hasExtension(filePath, {".png", ".jpg", ".jpeg", ".bmp", ".tga"});
    }

    bool FileUtils::isMeshFile(std::string_view filePath)
    {
        return hasExtension(filePath, {".obj", ".fbx", ".dae", ".gltf", ".glb"});
    }

    bool FileUtils::isAudioFile(std::string_view filePath)
    {
        return hasExtension(filePath, {".mp3", ".wav", ".ogg"});
    }
}
