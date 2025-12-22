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
        std::string outputPath;     // Required
    };

    class OrmTexturePacker
    {
    public:
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
            OrmPackProgressCallback progressCallback = nullptr);
    };
}
