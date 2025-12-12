#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"

namespace render {
	RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain, core::OffscreenResources& offscreenResources) : device{ device },
		swapChain{ swapChain }, offscreenResources{ offscreenResources }
		, clearColor{ std::make_unique<ClearColor>(device, swapChain, offscreenResources) }
		, iblRenderer{ std::make_unique<IBL>(device, swapChain, offscreenResources) }
		, meshPipeline{ std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources) }
	{
	}

	RenderPassHandler::~RenderPassHandler() = default;

	void RenderPassHandler::init()
	{
		clearColor->init();
		// meshPipeline->init() is called later via initMeshPipeline() when IBL textures are ready
	}

	void RenderPassHandler::initMeshPipeline()
	{
		if (meshPipelineInitialized)
		{
			return;
		}

		// Check if IBL is initialized - if so, use its textures; otherwise use defaults
		if (iblRenderer->isInitialized())
		{
			const auto& irradiance = iblRenderer->getIrradianceImage();
			const auto& prefilter = iblRenderer->getPrefilterImage();
			const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
			meshPipeline->init(irradiance, prefilter, brdfLUT);
		}
		else
		{
			// Use default placeholder textures for mesh rendering without IBL
			meshPipeline->initWithDefaults();
		}
		meshPipelineInitialized = true;
	}

	void RenderPassHandler::reinitMeshPipelineWithDefaults()
	{
		if (!meshPipelineInitialized)
		{
			return;
		}

		// Wait for GPU to finish using current resources
		device.getLogicalDevice().waitIdle();

		// Clean up current mesh pipeline resources (preserves loaded meshes)
		meshPipeline->cleanUpForReinit();

		// Reinitialize with default textures
		meshPipeline->initWithDefaults();
	}

	void RenderPassHandler::reinitMeshPipelineWithIBL()
	{
		if (!meshPipelineInitialized)
		{
			return;
		}

		// IBL must be initialized
		if (!iblRenderer->isInitialized())
		{
			return;
		}

		// Wait for GPU to finish using current resources
		device.getLogicalDevice().waitIdle();

		// Clean up current mesh pipeline resources (preserves loaded meshes)
		meshPipeline->cleanUpForReinit();

		// Reinitialize with IBL textures
		const auto& irradiance = iblRenderer->getIrradianceImage();
		const auto& prefilter = iblRenderer->getPrefilterImage();
		const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
		meshPipeline->init(irradiance, prefilter, brdfLUT);
	}

	void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData> meshes)
	{
		currentMeshDrawList = std::move(meshes);
	}

	void RenderPassHandler::recreate() const
	{
		iblRenderer->recreate();
		clearColor->recreate();

		if (meshPipelineInitialized)
		{
			meshPipeline->recreate();
		}
	}

	void RenderPassHandler::cleanUp() const
	{
		if (meshPipelineInitialized)
		{
			meshPipeline->cleanUp();
			meshPipeline->cleanUpShader();
		}

		iblRenderer->cleanUp();
		clearColor->cleanUp();
	}

	void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
	{
		clearColor->recordCommandBuffer(commandBuffer, imageIndex);
		iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

		// Render meshes after skybox
		if (meshPipelineInitialized && !currentMeshDrawList.empty())
		{
			meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, currentMeshDrawList);
		}
	}

}
