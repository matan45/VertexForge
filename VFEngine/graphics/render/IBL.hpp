#pragma once
#include "ibl/IBLTypes.hpp"
#include <glm/glm.hpp>
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
    namespace ibl
    {
        class EnvironmentCubemapGenerator;
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

        // Set camera matrices for skybox rendering (works with EditorCamera or CameraComponent)
        void setCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        
        // Disable skybox rendering
        void disableCamera();

        const ibl::ImageData& getBrdfLUTImage() const;
        const ibl::ImageData& getPrefilterImage() const;
        const ibl::ImageData& getIrradianceImage() const;

        // Check if IBL textures have been generated (init() was called)
        bool isInitialized() const { return iblInitialized; }

    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        std::shared_ptr<core::Texture> hdrTexture;
        bool isDisplay = false;
        bool iblInitialized = false;

        std::unique_ptr<ibl::EnvironmentCubemapGenerator> envCubemapGen;
        std::unique_ptr<ibl::IrradianceGenerator> irradianceGen;
        std::unique_ptr<ibl::BRDFLUTGenerator> brdfLUTGen;
        std::unique_ptr<ibl::PrefilteredEnvGenerator> prefilteredGen;
        std::unique_ptr<ibl::SkyboxRenderer> skyboxRenderer;
    };
}
