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
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        std::shared_ptr<core::Texture> hdrTexture;
        bool iblInitialized = false;

        std::unique_ptr<ibl::EnvironmentCubemapGenerator> envCubemapGen;
        std::unique_ptr<ibl::IrradianceGenerator> irradianceGen;
        std::unique_ptr<ibl::BRDFLUTGenerator> brdfLUTGen;
        std::unique_ptr<ibl::PrefilteredEnvGenerator> prefilteredGen;
        std::unique_ptr<ibl::SkyboxRenderer> skyboxRenderer;

    public:
        explicit IBL(core::Device& device, core::SwapChain& swapChain,
                     core::OffscreenResources& offscreenResources);
        ~IBL();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        void renderSkyboxToTarget(const vk::CommandBuffer& commandBuffer,
                                  const ibl::SkyboxTargetParams& target) const;
        void renderSkyboxToTarget(const vk::CommandBuffer& commandBuffer,
                                  const ibl::SkyboxTargetParams& target,
                                  vk::DescriptorSet targetDescriptorSet) const;
        vk::DescriptorSet createExternalSkyboxDescriptorSet(vk::Buffer externalCameraUBO,
                                                            vk::DescriptorPool externalPool) const;

        void init(std::string_view path);

        // VK-1574: initialize only the SkyboxRenderer, bound to an externally-owned live env cube
        // (HdrEnvironmentCapture), without the blocking generator bake. The env view is stable, so
        // this runs once; later HDR applies swap the cube's CONTENT via the capture's publish().
        void initSkybox(const ibl::ImageData& envCube);

        void remove();
        void cleanUp();

        // Set camera matrices for skybox rendering (works with EditorCamera or CameraComponent)
        void setCameraMatrices(const glm::mat4& view, const glm::mat4& projection);

        const ibl::ImageData& getBrdfLUTImage() const;
        const ibl::ImageData& getPrefilterImage() const;
        const ibl::ImageData& getIrradianceImage() const;

        // Check if IBL textures have been generated (init() was called)
        bool isInitialized() const { return iblInitialized; }

        // Check if the skybox is drawable. NOT the same as isInitialized(): the VK-1574 initSkybox()
        // path binds a live env cube without running the irradiance/prefilter generators, so
        // iblInitialized stays false while the skybox is perfectly renderable. Anything gating a
        // skybox DRAW must use this; only consumers of getIrradiance/getPrefilter/getBrdfLUT want
        // isInitialized().
        bool isSkyboxInitialized() const;
    };
}
