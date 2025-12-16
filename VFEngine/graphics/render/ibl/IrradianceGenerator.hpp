#pragma once
#include "IBLTypes.hpp"
#include <memory>

namespace core
{
    class Device;
    class Shader;
    class Texture;
}

namespace render::ibl
{
    class IrradianceGenerator
    {
    private:
        core::Device& device;
        ImageData imageIrradianceCube{};
        std::shared_ptr<core::Shader> shaderIrradianceCube;

    public:
        explicit IrradianceGenerator(core::Device& device);
        ~IrradianceGenerator() = default;

        void generate(const core::Texture& hdrTexture, const vk::CommandPool& commandPool);
        void cleanUp();
        void cleanUpShader();

        const ImageData& getImageData() const { return imageIrradianceCube; }

    private:
        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                                 const vk::DeviceMemory& uniformBufferMemory) const;
    };
}
