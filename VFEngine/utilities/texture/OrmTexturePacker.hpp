#pragma once
#include <string>
#include <functional>
#include "../resource/Types.hpp"

namespace texture
{
    using OrmPackProgressCallback = std::function<void(float progress)>;

    struct OrmPackResult {
        bool success = false;
        std::string outputPath;
        std::string errorMessage;
    };

    // Input paths for ORM packing - all optional
    // At least one texture must be provided to determine output dimensions
    struct OrmPackInput {
        std::string aoPath;         // Optional - defaults to 255 (no occlusion)
        std::string roughnessPath;  // Optional - defaults to 128 (mid roughness)
        std::string metallicPath;   // Optional - defaults to 0 (non-metallic)
        std::string emissivePath;   // Optional - defaults to 0 (no emission)
        std::string outputPath;     // Required
    };

    class OrmTexturePacker
    {
    public:
        // Pack textures into a single ORME texture
        // R = AO, G = Roughness, B = Metallic, A = Emissive
        // All input textures are optional - missing ones use default values
        static OrmPackResult packORM(
            const OrmPackInput& input,
            OrmPackProgressCallback progressCallback = nullptr);

        // Pack from already-loaded texture data (all optional, nullptr for defaults)
        static OrmPackResult packORMFromData(
            const resource::TextureData* aoTexture,
            const resource::TextureData* roughnessTexture,
            const resource::TextureData* metallicTexture,
            const resource::TextureData* emissiveTexture,
            const std::string& outputPath,
            OrmPackProgressCallback progressCallback = nullptr);

    private:
        static uint8_t getGrayscaleValue(const resource::MipLevelData& mipData,
                                          uint32_t x, uint32_t y,
                                          uint32_t width, uint32_t channels);
    };
}
