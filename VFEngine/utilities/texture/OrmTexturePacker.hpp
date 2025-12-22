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

    class OrmTexturePacker
    {
    public:
        // Pack 3 .vfImage textures into a single ORM texture
        // R = AO, G = Roughness, B = Metallic
        static OrmPackResult packORM(
            const std::string& aoPath,
            const std::string& roughnessPath,
            const std::string& metallicPath,
            const std::string& outputPath,
            OrmPackProgressCallback progressCallback = nullptr);

        // Pack from already-loaded texture data
        static OrmPackResult packORMFromData(
            const resource::TextureData& aoTexture,
            const resource::TextureData& roughnessTexture,
            const resource::TextureData& metallicTexture,
            const std::string& outputPath,
            OrmPackProgressCallback progressCallback = nullptr);

    private:
        static uint8_t getGrayscaleValue(const resource::MipLevelData& mipData,
                                          uint32_t x, uint32_t y,
                                          uint32_t width, uint32_t channels);
    };
}
