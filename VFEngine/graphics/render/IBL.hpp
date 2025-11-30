#pragma once
#include "ibl/IBLTypes.hpp"
#include "components/Components.hpp"
#include <memory>
#include <string_view>

namespace core
{
    class Device;
    class SwapChain;
    class Texture;
    struct OffscreenResources;
}

namespace render
{
    // Forward declarations for internal classes
    namespace ibl
    {
        class IrradianceGenerator;
        class BRDFLUTGenerator;
        class PrefilteredEnvGenerator;
        class SkyboxRenderer;
    }

    // Re-export types for backward compatibility
    using ibl::ImageData;
    using ibl::UniformBufferObject;
    using ibl::CameraViewMatrix;
    using ibl::OffScreenHelper;
    using ibl::QuadVertex;
    using ibl::cubeVertices;
    using ibl::skyboxVertices;
    using ibl::quad;

    class IBL
    {
    public:
        explicit IBL(core::Device& device, core::SwapChain& swapChain,
                     core::OffscreenResources& offscreenResources);
        ~IBL();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        void init(std::string_view path);
        void recreate();
        void remove();
        void cleanUp();

        void setCamera(components::CameraComponent* camera);

        const ibl::ImageData& getBrdfLUTImage() const;
        const ibl::ImageData& getPrefilterImage() const;

    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        vk::UniqueCommandPool commandPool;
        std::shared_ptr<core::Texture> hdrTexture;
        bool isDisplay = false;

        std::unique_ptr<ibl::IrradianceGenerator> irradianceGen;
        std::unique_ptr<ibl::BRDFLUTGenerator> brdfLUTGen;
        std::unique_ptr<ibl::PrefilteredEnvGenerator> prefilteredGen;
        std::unique_ptr<ibl::SkyboxRenderer> skyboxRenderer;
    };
}
