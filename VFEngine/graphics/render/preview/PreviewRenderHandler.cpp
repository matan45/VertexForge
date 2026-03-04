#include "PreviewRenderHandler.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../ClearColor.hpp"
#include "../IBL.hpp"
#include "../mesh/StaticMeshPipeline.hpp"
#include "../mesh/MeshTypes.hpp"

namespace render::preview
{
    PreviewRenderHandler::PreviewRenderHandler(core::Device& device, core::SwapChain& swapChain,
                                                core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , clearColor{std::make_unique<ClearColor>(device, swapChain, offscreenResources)}
        , iblRenderer{std::make_unique<IBL>(device, swapChain, offscreenResources)}
        , meshPipeline{std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources)}
    {
    }

    PreviewRenderHandler::~PreviewRenderHandler() = default;

    void PreviewRenderHandler::init()
    {
        clearColor->init();
    }

    void PreviewRenderHandler::initMeshPipeline()
    {
        if (meshPipelineInitialized)
        {
            return;
        }

        if (iblRenderer->isInitialized())
        {
            const auto& irradiance = iblRenderer->getIrradianceImage();
            const auto& prefilter = iblRenderer->getPrefilterImage();
            const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
            meshPipeline->init(irradiance, prefilter, brdfLUT);
        }
        else
        {
            meshPipeline->initWithDefaults();
        }
        meshPipelineInitialized = true;
    }

    void PreviewRenderHandler::reinitMeshPipelineWithDefaults()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();
        meshPipeline->initWithDefaults();
    }

    void PreviewRenderHandler::reinitMeshPipelineWithIBL()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        if (!iblRenderer->isInitialized())
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();

        const auto& irradiance = iblRenderer->getIrradianceImage();
        const auto& prefilter = iblRenderer->getPrefilterImage();
        const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
        meshPipeline->init(irradiance, prefilter, brdfLUT);
    }

    void PreviewRenderHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
    {
        currentMeshDrawList = std::move(meshes);
    }

    void PreviewRenderHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);
        iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

        if (meshPipelineInitialized && !currentMeshDrawList.empty())
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, currentMeshDrawList, currentFrustum,
                                               nullptr, glm::mat4{1.0f}, glm::mat4{1.0f});
        }
    }

    void PreviewRenderHandler::recreate()
    {
        iblRenderer->recreate();
        clearColor->recreate();

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();
        }
    }

    void PreviewRenderHandler::cleanUp() const
    {
        if (meshPipelineInitialized)
        {
            meshPipeline->cleanUpShader();
        }

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }
}
