#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "config/Config.hpp"

struct aiScene;

namespace types
{
    // VK-1641: import the textures referenced by the scene's materials as .vfImage
    // assets. External file references are resolved relative to sourceDir (with a
    // filename-only fallback for absolute paths baked by other tools); "*N" references
    // are pulled from the embedded texture array. Each distinct source is imported
    // once and the written .vfImage/.vfHdr path is appended to outWrittenTextures.
    // stb-decodable formats go through Texture::loadTextureFile; KTX2/KTX1/DDS through
    // Texture::loadKtxFile / loadDdsFile (VK-1642). Materials themselves are NOT
    // created or wired — the imported images are made available for the user to
    // author materials from.
    void importMaterialTextures(const aiScene* scene, const std::filesystem::path& sourceDir,
                                std::string_view location, std::string_view fileStem,
                                const importConfig::ImportConfig& config,
                                std::vector<std::string>& outWrittenTextures);
}
