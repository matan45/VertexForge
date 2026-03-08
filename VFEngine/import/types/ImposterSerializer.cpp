#include "ImposterSerializer.hpp"
#include "print/Log.hpp"
#include <fstream>
#include <filesystem>

namespace importTypes
{
    namespace fs = std::filesystem;

    bool ImposterSerializer::save(const std::string& filePath, const ImposterAtlasData& atlas)
    {
        if (atlas.atlasWidth == 0 || atlas.atlasHeight == 0 || atlas.views.empty())
        {
            vfLogError("ImposterSerializer: Cannot save empty atlas data");
            return false;
        }

        // Ensure parent directory exists
        fs::path path(filePath);
        if (path.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec)
            {
                vfLogError("ImposterSerializer: Failed to create directory {}: {}",
                           path.parent_path().string(), ec.message());
                return false;
            }
        }

        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("ImposterSerializer: Failed to open file for writing: {}", filePath);
            return false;
        }

        // 1. Magic: "VFIM" (4 bytes)
        file.write(IMPOSTER_MAGIC.data(), 4);

        // 2. Version: uint32_t
        uint32_t version = IMPOSTER_FORMAT_VERSION;
        file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));

        // 3. Atlas width: uint32_t
        file.write(reinterpret_cast<const char*>(&atlas.atlasWidth), sizeof(uint32_t));

        // 4. Atlas height: uint32_t
        file.write(reinterpret_cast<const char*>(&atlas.atlasHeight), sizeof(uint32_t));

        // 5. View count: uint32_t
        uint32_t viewCount = static_cast<uint32_t>(atlas.views.size());
        file.write(reinterpret_cast<const char*>(&viewCount), sizeof(uint32_t));

        // 6. Has normal map: uint8_t
        uint8_t hasNormalMap = atlas.config.generateNormalMap ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&hasNormalMap), sizeof(uint8_t));

        // 7. Config: horizontalAngles, verticalAngles, viewResolution (3 x uint32_t)
        file.write(reinterpret_cast<const char*>(&atlas.config.horizontalAngles), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&atlas.config.verticalAngles), sizeof(uint32_t));
        file.write(reinterpret_cast<const char*>(&atlas.config.viewResolution), sizeof(uint32_t));

        // 8. View infos: per view (2 floats angles + 4 floats uvRect)
        for (const auto& view : atlas.views)
        {
            file.write(reinterpret_cast<const char*>(&view.horizontalAngle), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.verticalAngle), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.z), sizeof(float));
            file.write(reinterpret_cast<const char*>(&view.uvRect.w), sizeof(float));
        }

        // 9. Color data: width*height*4 bytes (RGBA8)
        size_t pixelDataSize = static_cast<size_t>(atlas.atlasWidth) * atlas.atlasHeight * 4;
        if (atlas.colorData.size() != pixelDataSize)
        {
            vfLogError("ImposterSerializer: Color data size mismatch. Expected {}, got {}",
                       pixelDataSize, atlas.colorData.size());
            return false;
        }
        file.write(reinterpret_cast<const char*>(atlas.colorData.data()),
                   static_cast<std::streamsize>(pixelDataSize));

        // 10. Normal data: width*height*4 bytes (if has normal map)
        if (hasNormalMap)
        {
            if (atlas.normalData.size() != pixelDataSize)
            {
                vfLogError("ImposterSerializer: Normal data size mismatch. Expected {}, got {}",
                           pixelDataSize, atlas.normalData.size());
                return false;
            }
            file.write(reinterpret_cast<const char*>(atlas.normalData.data()),
                       static_cast<std::streamsize>(pixelDataSize));
        }

        if (!file.good())
        {
            vfLogError("ImposterSerializer: Write error saving atlas to {}", filePath);
            return false;
        }

        return true;
    }

    bool ImposterSerializer::load(const std::string& filePath, ImposterAtlasData& atlas)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("ImposterSerializer: Failed to open file for reading: {}", filePath);
            return false;
        }

        // 1. Validate magic number
        std::array<char, 4> magic{};
        file.read(magic.data(), 4);
        if (magic != IMPOSTER_MAGIC)
        {
            vfLogError("ImposterSerializer: Invalid magic bytes in file: {}", filePath);
            return false;
        }

        // 2. Version check
        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        if (version != IMPOSTER_FORMAT_VERSION)
        {
            vfLogError("ImposterSerializer: Incompatible version {}, expected {}. Re-import required.",
                       version, IMPOSTER_FORMAT_VERSION);
            return false;
        }

        // 3. Atlas width
        file.read(reinterpret_cast<char*>(&atlas.atlasWidth), sizeof(uint32_t));

        // 4. Atlas height
        file.read(reinterpret_cast<char*>(&atlas.atlasHeight), sizeof(uint32_t));

        if (atlas.atlasWidth == 0 || atlas.atlasHeight == 0 ||
            atlas.atlasWidth > MAX_IMPOSTER_ATLAS_DIM || atlas.atlasHeight > MAX_IMPOSTER_ATLAS_DIM)
        {
            vfLogError("ImposterSerializer: Invalid atlas dimensions {}x{} in file: {}",
                       atlas.atlasWidth, atlas.atlasHeight, filePath);
            return false;
        }

        // 5. View count
        uint32_t viewCount = 0;
        file.read(reinterpret_cast<char*>(&viewCount), sizeof(uint32_t));
        if (viewCount == 0 || viewCount > MAX_IMPOSTER_VIEW_COUNT)
        {
            vfLogError("ImposterSerializer: Invalid view count {} in file: {}", viewCount, filePath);
            return false;
        }

        // 6. Has normal map
        uint8_t hasNormalMap = 0;
        file.read(reinterpret_cast<char*>(&hasNormalMap), sizeof(uint8_t));
        atlas.config.generateNormalMap = (hasNormalMap != 0);

        // 7. Config
        file.read(reinterpret_cast<char*>(&atlas.config.horizontalAngles), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&atlas.config.verticalAngles), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&atlas.config.viewResolution), sizeof(uint32_t));

        // 8. View infos
        atlas.views.clear();
        atlas.views.reserve(viewCount);
        for (uint32_t i = 0; i < viewCount; ++i)
        {
            ImposterViewInfo view;
            file.read(reinterpret_cast<char*>(&view.horizontalAngle), sizeof(float));
            file.read(reinterpret_cast<char*>(&view.verticalAngle), sizeof(float));
            file.read(reinterpret_cast<char*>(&view.uvRect.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&view.uvRect.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&view.uvRect.z), sizeof(float));
            file.read(reinterpret_cast<char*>(&view.uvRect.w), sizeof(float));
            atlas.views.push_back(view);
        }

        // 9. Color data
        size_t pixelDataSize = static_cast<size_t>(atlas.atlasWidth) * atlas.atlasHeight * 4;
        atlas.colorData.resize(pixelDataSize);
        file.read(reinterpret_cast<char*>(atlas.colorData.data()),
                  static_cast<std::streamsize>(pixelDataSize));

        // 10. Normal data (if present)
        if (hasNormalMap)
        {
            atlas.normalData.resize(pixelDataSize);
            file.read(reinterpret_cast<char*>(atlas.normalData.data()),
                      static_cast<std::streamsize>(pixelDataSize));
        }
        else
        {
            atlas.normalData.clear();
        }

        if (!file.good())
        {
            vfLogError("ImposterSerializer: Read error loading atlas from {}", filePath);
            atlas = ImposterAtlasData{};
            return false;
        }

        return true;
    }
}
