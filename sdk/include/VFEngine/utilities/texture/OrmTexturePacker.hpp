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
    using TextureCompressCallback = std::function<void(resource::TextureData& textureData)>;

    // Callback for decompressing a compressed TextureData in-place before ORM packing
    using TextureDecompressCallback = std::function<bool(resource::TextureData& textureData)>;

    // Input paths for ORM packing - all optional
    // At least one texture must be provided to determine output dimensions
    struct OrmPackInput {
        std::string aoPath;         // Optional - defaults to 255 (no occlusion)
        std::string roughnessPath;  // Optional - defaults to 128 (mid roughness)
        std::string metallicPath;   // Optional - defaults to 0 (non-metallic)
        // VK-1609 - Optional; defaults to 128 (neutral height). Consumed by terrain height-blended
        // layer compositing, which reads per-layer height from ORM alpha so it costs no extra
        // texture fetch. Grayscale input; only the red byte of each texel is read.
        std::string heightPath;
        std::string outputPath;     // Required
        TextureCompressCallback compressCallback;       // Optional - compresses output before saving
        TextureDecompressCallback decompressCallback;   // Optional - decompresses compressed inputs before packing
    };

    class OrmTexturePacker
    {
    public:
        // Default values for missing textures
        static constexpr uint8_t DEFAULT_AO = 255;        // No occlusion (fully lit)
        static constexpr uint8_t DEFAULT_ROUGHNESS = 128; // Mid roughness (~0.5)
        static constexpr uint8_t DEFAULT_METALLIC = 0;    // Non-metallic
        // VK-1609. Neutral height, NOT 255: the terrain composite centres its exponential on 0.5,
        // so a mid-grey alpha makes "no height authored" a true no-op. (Neither 127 nor 128 is
        // exactly 0.5 in 8-bit; the half-LSB bias is inherent and negligible.)
        static constexpr uint8_t DEFAULT_HEIGHT = 128;

        // Pack textures into a single ORM texture (RGBA for GPU compatibility)
        // R = AO, G = Roughness, B = Metallic, A = Height (VK-1609 terrain height blending)
        // All input textures are optional - missing ones use default values
        //
        // NOTE on compression: the vendored BC7 encoder emits mode 6 only, which shares one 4-bit
        // index across RGBA. A height signal uncorrelated with AO/roughness/metallic therefore
        // quantizes poorly AND drags RGB down with it. Author height-bearing terrain ORM textures
        // uncompressed until the encoder learns modes 4/5 (separate alpha index).
        static OrmPackResult packORM(
            const OrmPackInput& input,
            OrmPackProgressCallback progressCallback = nullptr);

        // Pack from already-loaded texture data (all optional, nullptr for defaults)
        static OrmPackResult packORMFromData(
            const resource::TextureData* aoTexture,
            const resource::TextureData* roughnessTexture,
            const resource::TextureData* metallicTexture,
            const resource::TextureData* heightTexture,
            const std::string& outputPath,
            OrmPackProgressCallback progressCallback = nullptr,
            TextureCompressCallback compressCallback = nullptr);
    };
}
