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
    class EnvironmentCubemapGenerator
    {
    private:
        core::Device& device;
        ImageData imageEnvCubemap{};
        std::shared_ptr<core::Shader> shaderEnvCubemap;
        
    public:
        explicit EnvironmentCubemapGenerator(core::Device& device);
        ~EnvironmentCubemapGenerator() = default;

        void generate(const core::Texture& hdrTexture, const vk::CommandPool& commandPool);
        void cleanUp();
        void cleanUpShader();

        const ImageData& getImageData() const { return imageEnvCubemap; }

    private:
        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                                 const vk::DeviceMemory& uniformBufferMemory) const;
    };
}
