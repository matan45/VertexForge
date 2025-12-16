#include "IBL.hpp"
#include "ibl/EnvironmentCubemapGenerator.hpp"
#include "ibl/IrradianceGenerator.hpp"
#include "ibl/BRDFLUTGenerator.hpp"
#include "ibl/PrefilteredEnvGenerator.hpp"
#include "ibl/SkyboxRenderer.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Texture.hpp"
#include <iostream>

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
        std::cout << "[DEBUG IBL] Starting init with path: " << path << std::endl;

        hdrTexture = std::make_shared<core::Texture>(device);
        hdrTexture->loadHDRFromFile(path, vk::Format::eR32G32B32A32Sfloat, false);
        std::cout << "[DEBUG IBL] HDR texture loaded" << std::endl;

        // Generate environment cubemap (sharp, for skybox and prefilter input)
        std::cout << "[DEBUG IBL] Generating environment cubemap..." << std::endl;
        envCubemapGen->generate(*hdrTexture, commandPool.get());
        std::cout << "[DEBUG IBL] Environment cubemap generated" << std::endl;

        // Generate irradiance map (convolved/blurry, for diffuse IBL)
        std::cout << "[DEBUG IBL] Generating irradiance map..." << std::endl;
        irradianceGen->generate(*hdrTexture, commandPool.get());
        std::cout << "[DEBUG IBL] Irradiance map generated" << std::endl;

        std::cout << "[DEBUG IBL] Generating BRDF LUT..." << std::endl;
        brdfLUTGen->generate(commandPool.get());
        std::cout << "[DEBUG IBL] BRDF LUT generated" << std::endl;

        // Prefilter uses environment cubemap (not irradiance)
        std::cout << "[DEBUG IBL] Generating prefiltered env map..." << std::endl;
        prefilteredGen->generate(envCubemapGen->getImageData(), commandPool.get());
        std::cout << "[DEBUG IBL] Prefiltered env map generated" << std::endl;

        // Skybox uses environment cubemap (sharp)
        std::cout << "[DEBUG IBL] Initializing skybox renderer..." << std::endl;
        skyboxRenderer->init(envCubemapGen->getImageData());
        std::cout << "[DEBUG IBL] Skybox renderer initialized" << std::endl;

        iblInitialized = true;
        std::cout << "[DEBUG IBL] Init complete!" << std::endl;
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
