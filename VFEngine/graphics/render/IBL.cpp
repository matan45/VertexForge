#include "IBL.hpp"
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
        hdrTexture->loadHDRFromFile(path, vk::Format::eR32G32B32Sfloat, false);

        irradianceGen->generate(*hdrTexture, commandPool.get());
        brdfLUTGen->generate(commandPool.get());
        prefilteredGen->generate(irradianceGen->getImageData(), commandPool.get());
        skyboxRenderer->init(irradianceGen->getImageData());
    }

    void IBL::recreate()
    {
        skyboxRenderer->recreate();
    }

    void IBL::remove()
    {
        if (isDisplay)
        {
            isDisplay = false;
            hdrTexture.reset();

            skyboxRenderer->cleanUp();
            irradianceGen->cleanUp();
            brdfLUTGen->cleanUp();
            prefilteredGen->cleanUp();
        }
    }

    void IBL::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        skyboxRenderer->cleanUpShader();
        irradianceGen->cleanUpShader();
        brdfLUTGen->cleanUpShader();
        prefilteredGen->cleanUpShader();

        commandPool.reset();
        remove();
    }

    void IBL::setCamera(components::CameraComponent* camera)
    {
        skyboxRenderer->setCamera(camera);
        isDisplay = (camera != nullptr);
    }

    const ibl::ImageData& IBL::getBrdfLUTImage() const
    {
        return brdfLUTGen->getImageData();
    }

    const ibl::ImageData& IBL::getPrefilterImage() const
    {
        return prefilteredGen->getImageData();
    }
}
