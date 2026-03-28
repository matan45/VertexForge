#pragma once
#include "IBLTypes.hpp"
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::ibl
{
    class PrefilteredEnvGenerator
    {
    private:
        core::Device& device;
        ImageData prefilterImage{};
        std::shared_ptr<core::Shader> prefilterShader;

    public:
        explicit PrefilteredEnvGenerator(core::Device& device);
        ~PrefilteredEnvGenerator() = default;

        void generate(const ImageData& irradianceCube, const vk::CommandPool& commandPool);
        void cleanUp();
        void cleanUpShader();

        const ImageData& getImageData() const { return prefilterImage; }

    private:
        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                                 core::VulkanAllocation& uniformBufferAllocation) const;
    };
}
