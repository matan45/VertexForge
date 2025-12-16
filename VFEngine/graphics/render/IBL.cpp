#include "IBL.hpp"
#include "ibl/EnvironmentCubemapGenerator.hpp"
#include "ibl/IrradianceGenerator.hpp"
#include "ibl/BRDFLUTGenerator.hpp"
#include "ibl/PrefilteredEnvGenerator.hpp"
#include "ibl/SkyboxRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Texture.hpp"

namespace render
{
    IBL::IBL(core::Device& device, core::SwapChain& swapChain,
             core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
    {
        // Create command pool
        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        commandPoolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPoolUnique(commandPoolInfo);

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

    void IBL::init(std::string_view path)
    {
        hdrTexture = std::make_shared<core::Texture>(device);
        hdrTexture->loadHDRFromFile(path, vk::Format::eR32G32B32A32Sfloat, false);

        // Generate environment cubemap (sharp, for skybox and prefilter input)
        envCubemapGen->generate(*hdrTexture, commandPool.get());

        // Generate irradiance map (convolved/blurry, for diffuse IBL)
        irradianceGen->generate(*hdrTexture, commandPool.get());

        brdfLUTGen->generate(commandPool.get());

        // Prefilter uses environment cubemap (not irradiance)
        prefilteredGen->generate(envCubemapGen->getImageData(), commandPool.get());

        // Skybox uses environment cubemap (sharp)
        skyboxRenderer->init(envCubemapGen->getImageData());

        iblInitialized = true;
    }

    void IBL::recreate()
    {
        skyboxRenderer->recreate();
    }

    void IBL::remove()
    {
        if (iblInitialized)
        {
            device.getLogicalDevice().waitIdle();

            // Disable rendering first to prevent access during cleanup
            skyboxRenderer->disable();
            isDisplay = false;
            iblInitialized = false;

            hdrTexture.reset();

            skyboxRenderer->cleanUp();
            envCubemapGen->cleanUp();
            irradianceGen->cleanUp();
            brdfLUTGen->cleanUp();
            prefilteredGen->cleanUp();
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

        commandPool.reset();
        remove();
    }

    void IBL::setCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        skyboxRenderer->setCameraMatrices(view, projection);
        isDisplay = true;
    }

    void IBL::disableCamera()
    {
        skyboxRenderer->disable();
        isDisplay = false;
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
