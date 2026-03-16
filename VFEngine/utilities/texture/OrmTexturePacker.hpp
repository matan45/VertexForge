#pragma once
#include <string>
#include <functional>
#include "../resource/Types.hpp"
#include "../config/Config.hpp"

namespace texture
{
    using OrmPackProgressCallback = std::function<void(float progress)>;

    struct OrmPackResult {
        bool success = false;
        std::string outputPath;
        std::string errorMessage;
    };

    // Callback for compressing a TextureData in-place after ORM channel packing
    // Called before serialization. Provided by the editor via TextureCompressor.
    using TextureCompressCallback = std::function<void(resource::TextureData& textureData)>;

    // Input paths for ORM packing - all optional
    // At least one texture must be provided to determine output dimensions
    struct OrmPackInput {
        std::string aoPath;         // Optional - defaults to 255 (no occlusion)
        std::string roughnessPath;  // Optional - defaults to 128 (mid roughness)
        std::string metallicPath;   // Optional - defaults to 0 (non-metallic)
        std::string outputPath;     // Required
        TextureCompressCallback compressCallback; // Optional - compresses output before saving
    };

    class OrmTexturePacker
    {
    public:
        // Default values for missing textures
        static constexpr uint8_t DEFAULT_AO = 255;        // No occlusion (fully lit)
        static constexpr uint8_t DEFAULT_ROUGHNESS = 128; // Mid roughness (~0.5)
        static constexpr uint8_t DEFAULT_METALLIC = 0;    // Non-metallic

        // Pack textures into a single ORM texture (RGBA for GPU compatibility)
        // R = AO, G = Roughness, B = Metallic, A = 255 (unused)
        // All input textures are optional - missing ones use default values
        static OrmPackResult packORM(
            const OrmPackInput& input,
            OrmPackProgressCallback progressCallback = nullptr);

        // Pack from already-loaded texture data (all optional, nullptr for defaults)
        static OrmPackResult packORMFromData(
            const resource::TextureData* aoTexture,
            const resource::TextureData* roughnessTexture,
            const resource::TextureData* metallicTexture,
            const std::string& outputPath,
            OrmPackProgressCallback progressCallback = nullptr,
            TextureCompressCallback compressCallback = nullptr);
    };
}
