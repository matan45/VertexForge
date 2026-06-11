#include "FileUtils.hpp"
#include "string/StringUtil.hpp"
#include "../../registry/ImporterRegistry.hpp"
#include "../../registry/builtin/BuiltinImporters.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace files
{
    namespace
    {
        bool hasExtensionForAssetType(std::string_view filePath, resource::AssetType assetType)
        {
            import::builtin::ensureRegistered();

            std::string ext = FileUtils::getFileExtension(filePath);
            if (!ext.empty() && ext.front() == '.')
                ext.erase(0, 1);

            for (const auto& info : import::ImporterRegistry::instance().allFormats())
            {
                if (info.assetType != assetType)
                    continue;

                for (const auto& e : info.extensions)
                {
                    if (ext == e)
                        return true;
                }
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
        return hasExtensionForAssetType(filePath, resource::AssetType::HDR);
    }

    bool FileUtils::isTextureFile(std::string_view filePath)
    {
        return hasExtensionForAssetType(filePath, resource::AssetType::Texture);
    }

    bool FileUtils::isMeshFile(std::string_view filePath)
    {
        return hasExtensionForAssetType(filePath, resource::AssetType::Mesh);
    }

    bool FileUtils::isAudioFile(std::string_view filePath)
    {
        return hasExtensionForAssetType(filePath, resource::AssetType::Audio);
    }
}
