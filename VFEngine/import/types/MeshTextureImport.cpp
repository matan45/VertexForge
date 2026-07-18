#include "MeshTextureImport.hpp"
#include "Texture.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_set>

#include <assimp/scene.h>
#include <assimp/material.h>

namespace types
{
    namespace
    {
        // Strips characters illegal in Windows file names. Kept local to this TU (a
        // near-copy of Mesh.cpp's file-local helper) to avoid cross-TU coupling.
        std::string sanitizeStem(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            for (char c : name)
            {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' ||
                    c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
                    out.push_back('_');
                else
                    out.push_back(c);
            }

            const size_t start = out.find_first_not_of(" .");
            if (start == std::string::npos)
                return {};
            const size_t end = out.find_last_not_of(" .");
            return out.substr(start, end - start + 1);
        }

        std::string toLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Formats stb_image (used by Texture::loadTextureFile) can decode. Block-
        // compressed containers (dds/ktx/ktx2) are intentionally excluded — see VK-1642.
        bool isStbSupportedExtension(const std::string& extLower)
        {
            static const std::unordered_set<std::string> supported = {
                ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".gif", ".hdr", ".pic", ".ppm", ".pgm"
            };
            return supported.contains(extLower);
        }
    }

    void importMaterialTextures(const aiScene* scene, const std::filesystem::path& sourceDir,
                                std::string_view location, std::string_view fileStem,
                                const importConfig::ImportConfig& config,
                                std::vector<std::string>& outWrittenTextures)
    {
        if (!scene || scene->mNumMaterials == 0)
            return;

        Texture textureWriter;
        std::unordered_set<std::string> importedSources; // dedup by resolved source key
        std::unordered_set<std::string> usedStemsLower;   // dedup output stems (Windows paths)

        // Namespaces the texture stem under the model's stem and dedups case-insensitively.
        const auto uniqueStem = [&](const std::string& base)
        {
            const std::string prefixed =
                std::string(fileStem) + "_" + (base.empty() ? std::string("texture") : base);
            std::string unique = prefixed;
            unsigned int counter = 1;
            while (!usedStemsLower.insert(toLower(unique)).second)
                unique = prefixed + "_" + std::to_string(counter++);
            return unique;
        };

        const auto outPathFor = [&](const std::string& stem)
        {
            return (std::filesystem::path(location) / (stem + "." + FileExtension::textrue)).string();
        };

        for (unsigned int m = 0; m < scene->mNumMaterials; ++m)
        {
            const aiMaterial* material = scene->mMaterials[m];
            if (!material)
                continue;

            for (int t = aiTextureType_DIFFUSE; t <= aiTextureType_UNKNOWN; ++t)
            {
                const auto type = static_cast<aiTextureType>(t);
                const unsigned int count = material->GetTextureCount(type);
                for (unsigned int i = 0; i < count; ++i)
                {
                    aiString aiPath;
                    if (material->GetTexture(type, i, &aiPath) != AI_SUCCESS || aiPath.length == 0)
                        continue;

                    const std::string raw = aiPath.C_Str();

                    // Embedded reference ("*N"): pull the blob from aiScene->mTextures.
                    if (!raw.empty() && raw[0] == '*')
                    {
                        if (!importedSources.insert("embedded:" + raw).second)
                            continue;

                        const aiTexture* tex = scene->GetEmbeddedTexture(raw.c_str());
                        if (!tex || !tex->pcData)
                            continue;

                        std::string base = sanitizeStem(tex->mFilename.C_Str());
                        if (base.empty())
                            base = "embedded";
                        const std::string stem = uniqueStem(base);

                        // Assimp: mHeight == 0 => pcData is a compressed file blob of
                        // mWidth bytes; otherwise mWidth*mHeight uncompressed BGRA texels.
                        const bool compressed = tex->mHeight == 0;
                        const size_t byteLength = compressed
                                                      ? static_cast<size_t>(tex->mWidth)
                                                      : static_cast<size_t>(tex->mWidth) * tex->mHeight * 4;
                        if (textureWriter.saveEmbeddedTexture(stem, location,
                                                              reinterpret_cast<const unsigned char*>(tex->pcData),
                                                              byteLength, compressed, tex->mWidth, tex->mHeight,
                                                              config))
                        {
                            outWrittenTextures.push_back(outPathFor(stem));
                        }
                        continue;
                    }

                    // External file: resolve relative to the source model, with a
                    // filename-only fallback for absolute paths baked by another tool.
                    const std::filesystem::path rawPath(raw);
                    std::error_code ec;
                    std::filesystem::path resolved;
                    if (rawPath.is_absolute() && std::filesystem::exists(rawPath, ec))
                        resolved = rawPath;
                    else if (std::filesystem::exists(sourceDir / rawPath, ec))
                        resolved = sourceDir / rawPath;
                    else if (std::filesystem::exists(sourceDir / rawPath.filename(), ec))
                        resolved = sourceDir / rawPath.filename();

                    if (resolved.empty())
                    {
                        vfLogWarning("Material texture '{}' referenced by '{}' was not found next to the "
                                     "model; skipping",
                                     raw, fileStem);
                        continue;
                    }

                    if (!importedSources.insert(toLower(resolved.lexically_normal().string())).second)
                        continue;

                    const std::string extLower = toLower(resolved.extension().string());
                    if (!isStbSupportedExtension(extLower))
                    {
                        vfLogWarning("Material texture '{}' has unsupported format '{}' (KTX/KTX2/DDS need "
                                     "conversion, see VK-1642); skipping",
                                     resolved.string(), extLower);
                        continue;
                    }

                    const std::string stem = uniqueStem(sanitizeStem(resolved.stem().string()));
                    const importConfig::ImportFiles textureFile(resolved.string(), config);
                    textureWriter.loadTextureFile(textureFile, stem, location);

                    // loadTextureFile returns void and logs its own errors; treat the
                    // written .vfImage's existence as the success signal.
                    const std::string outPath = outPathFor(stem);
                    if (std::filesystem::exists(outPath, ec))
                        outWrittenTextures.push_back(outPath);
                }
            }
        }
    }
}
