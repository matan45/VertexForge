#include "IBL.hpp"
#include "ibl/EnvironmentCubemapGenerator.hpp"
#include "ibl/IrradianceGenerator.hpp"
#include "ibl/BRDFLUTGenerator.hpp"
#include "ibl/PrefilteredEnvGenerator.hpp"
#include "ibl/SkyboxRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Texture.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"

namespace render
{
    IBL::IBL(core::Device& device, core::SwapChain& swapChain,
             core::OffscreenResources& offscreenResources)
        : device{device}
          , swapChain{swapChain}
          , offscreenResources{offscreenResources}
    {
        // Create sub-components
        envCubemapGen = std::make_unique<ibl::EnvironmentCubemapGenerator>(device);
        irradianceGen = std::make_unique<ibl::IrradianceGenerator>(device);
        brdfLUTGen = std::make_unique<ibl::BRDFLUTGenerator>(device);
        prefilteredGen = std::make_unique<ibl::PrefilteredEnvGenerator>(device);
        skyboxRenderer = std::make_unique<ibl::SkyboxRenderer>(device, swapChain, offscreenResources);
    }

    IBL::~IBL() = default;

    void IBL::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        skyboxRenderer->recordCommandBuffer(commandBuffer, imageIndex);
    }

    void IBL::recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        skyboxRenderer->renderSkyGraphManaged(commandBuffer, imageIndex);
    }

    bool IBL::isSkyboxInitialized() const
    {
        return skyboxRenderer && skyboxRenderer->isInitialized();
    }

    void IBL::renderSkyboxToTarget(const vk::CommandBuffer& commandBuffer,
                                    const ibl::SkyboxTargetParams& target) const
    {
        // Guard on the skybox renderer directly (not iblInitialized) so RTT works with the VK-1574
        // live env cube too (initSkybox path, where iblInitialized stays false).
        if (skyboxRenderer && skyboxRenderer->isInitialized())
        {
            skyboxRenderer->renderToTarget(commandBuffer, target);
        }
    }

    void IBL::renderSkyboxToTarget(const vk::CommandBuffer& commandBuffer,
                                    const ibl::SkyboxTargetParams& target,
                                    vk::DescriptorSet targetDescriptorSet) const
    {
        if (skyboxRenderer && skyboxRenderer->isInitialized())
        {
            skyboxRenderer->renderToTarget(commandBuffer, target, targetDescriptorSet);
        }
    }

    vk::DescriptorSet IBL::createExternalSkyboxDescriptorSet(vk::Buffer externalCameraUBO,
                                                              vk::DescriptorPool externalPool) const
    {
        if (!skyboxRenderer || !skyboxRenderer->isInitialized())
        {
            return {};
        }

        return skyboxRenderer->createExternalDescriptorSet(externalCameraUBO, externalPool);
    }

    void IBL::init(std::string_view path)
    {
        if (iblInitialized)
        {
            remove();
        }

        hdrTexture = std::make_shared<core::Texture>(device);
        hdrTexture->loadHDRFromFile(path, false);

        envCubemapGen->generate(*hdrTexture, device.getStagingCommandPool());
        irradianceGen->generate(*hdrTexture, device.getStagingCommandPool());

        brdfLUTGen->generate(device.getStagingCommandPool());

        prefilteredGen->generate(envCubemapGen->getImageData(), device.getStagingCommandPool());
        skyboxRenderer->init(envCubemapGen->getImageData());

        iblInitialized = true;
    }

    void IBL::initSkybox(const ibl::ImageData& envCube)
    {
        // Bind the skybox to the externally-owned live env cube. The view is stable (written once by
        // HdrEnvironmentCapture), so init runs a single time; subsequent HDR applies only change the
        // cube's contents via the capture's atomic publish(), needing no rebind here. iblInitialized
        // stays false — the blocking generator maps were never produced, so the mesh IBL path (which
        // reads them) must not treat this as a full IBL bake.
        if (!skyboxRenderer->isInitialized())
            skyboxRenderer->init(envCube);
    }

    void IBL::remove()
    {
        if (iblInitialized)
        {
            device.getLogicalDevice().waitIdle();

            // Disable rendering first to prevent access during cleanup
            skyboxRenderer->disable();
            iblInitialized = false;

            hdrTexture.reset();

            skyboxRenderer->cleanUp();
            envCubemapGen->cleanUp();
            irradianceGen->cleanUp();
            brdfLUTGen->cleanUp();
            prefilteredGen->cleanUp();
        }
        else if (skyboxRenderer->isInitialized())
        {
            // VK-1574 initSkybox-only path: no generators ran (iblInitialized stayed false), so just
            // tear down the skybox that was bound to the externally-owned live env cube. Callers
            // destroy that cube (HdrEnvironmentCapture) only after this returns.
            device.getLogicalDevice().waitIdle();
            skyboxRenderer->disable();
            skyboxRenderer->cleanUp();
        }
    }

    void IBL::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        skyboxRenderer->cleanUpShader();
        envCubemapGen->cleanUpShader();
        irradianceGen->cleanUpShader();
        brdfLUTGen->cleanUpShader();
        prefilteredGen->cleanUpShader();

        remove();
    }

    void IBL::setCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        skyboxRenderer->setCameraMatrices(view, projection);
    }

    const ibl::ImageData& IBL::getBrdfLUTImage() const
    {
        return brdfLUTGen->getImageData();
    }

    const ibl::ImageData& IBL::getPrefilterImage() const
    {
        return prefilteredGen->getImageData();
    }

    const ibl::ImageData& IBL::getIrradianceImage() const
    {
        return irradianceGen->getImageData();
    }
}
