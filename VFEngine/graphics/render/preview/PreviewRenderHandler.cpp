#include "PreviewRenderHandler.hpp"
#include "PreviewBackgroundRenderer.hpp"
#include "PreviewGridRenderer.hpp"
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
        , backgroundRenderer{std::make_unique<PreviewBackgroundRenderer>(device, swapChain, offscreenResources)}
        , previewGrid{std::make_unique<PreviewGridRenderer>(device, swapChain, offscreenResources)}
    {
    }

    PreviewRenderHandler::~PreviewRenderHandler() = default;

    void PreviewRenderHandler::init()
    {
        clearColor->init();

        backgroundRenderer->init();
        backgroundInitialized = true;

        previewGrid->init();
        gridInitialized = true;
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
        // Apply environment background color
        if (envParams.backgroundMode == 0)
        {
            clearColor->setClearColor(envParams.backgroundColor);
        }
        else
        {
            // For gradient mode, use bottom color as clear base
            clearColor->setClearColor(envParams.gradientBottomColor);
        }

        clearColor->recordCommandBuffer(commandBuffer, imageIndex);

        // Draw gradient background if in gradient mode
        if (envParams.backgroundMode == 1 && backgroundInitialized)
        {
            backgroundRenderer->render(commandBuffer, imageIndex,
                                        envParams.gradientTopColor, envParams.gradientBottomColor);
        }

        iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

        if (meshPipelineInitialized && !currentMeshDrawList.empty())
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, currentMeshDrawList, currentFrustum,
                                               nullptr, cachedView, cachedProjection);
        }

        // Draw grid overlay in a separate render pass (loads existing color+depth)
        if (gridInitialized && envParams.showGrid)
        {
            previewGrid->render(commandBuffer, imageIndex, cachedView, cachedProjection, true);
        }
    }

    void PreviewRenderHandler::recreate()
    {
        if (backgroundInitialized)
        {
            backgroundRenderer->recreate();
        }

        if (gridInitialized)
        {
            previewGrid->recreate();
        }

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();
        }
    }

    void PreviewRenderHandler::cleanUp() const
    {
        if (gridInitialized)
        {
            previewGrid->cleanUpShader();
            previewGrid->cleanUp();
        }

        if (backgroundInitialized)
        {
            backgroundRenderer->cleanUpShader();
            backgroundRenderer->cleanUp();
        }

        if (meshPipelineInitialized)
        {
            meshPipeline->cleanUpShader();
        }

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }
}
