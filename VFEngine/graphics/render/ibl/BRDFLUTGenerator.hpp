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
    class BRDFLUTGenerator
    {
    public:
        explicit BRDFLUTGenerator(core::Device& device);
        ~BRDFLUTGenerator() = default;

        void generate(const vk::CommandPool& commandPool);
        void cleanUp();
        void cleanUpShader();

        const ImageData& getImageData() const { return brdfLUTImage; }

    private:
        core::Device& device;
        ImageData brdfLUTImage{};
        std::shared_ptr<core::Shader> brdfLUTShader;
    };
}
