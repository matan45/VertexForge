#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "billboard/BillboardTypes.hpp"

namespace render {
	RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain, core::OffscreenResources& offscreenResources) : device{ device },
		swapChain{ swapChain }, offscreenResources{ offscreenResources }
		, clearColor{ std::make_unique<ClearColor>(device, swapChain, offscreenResources) }
		, iblRenderer{ std::make_unique<IBL>(device, swapChain, offscreenResources) }
		, meshPipeline{ std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources) }
		, billboardPipeline{ std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources) }
	{
	}

	RenderPassHandler::~RenderPassHandler() = default;

	void RenderPassHandler::init()
	{
		clearColor->init();
	}

	void RenderPassHandler::initMeshPipeline()
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

	void RenderPassHandler::reinitMeshPipelineWithDefaults()
	{
		if (!meshPipelineInitialized)
		{
			return;
		}
		
		device.getLogicalDevice().waitIdle();
		
		meshPipeline->cleanUpForReinit();
		
		meshPipeline->initWithDefaults();
	}

	void RenderPassHandler::reinitMeshPipelineWithIBL()
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

	void RenderPassHandler::setMeshDrawList(const std::vector<mesh::MeshRenderData>& meshes)
	{
		currentMeshDrawList = meshes;
	}

	void RenderPassHandler::initBillboardPipeline()
	{
		if (billboardPipelineInitialized)
		{
			return;
		}

		billboardPipeline->init();
		billboardPipelineInitialized = true;
	}

	void RenderPassHandler::recreate() const
	{
		iblRenderer->recreate();
		clearColor->recreate();

		if (meshPipelineInitialized)
		{
			meshPipeline->recreate();
		}

		if (billboardPipelineInitialized)
		{
			billboardPipeline->recreate();
		}
	}

	void RenderPassHandler::cleanUp() const
	{
		if (billboardPipelineInitialized)
		{
			billboardPipeline->cleanUp();
		}

		if (meshPipelineInitialized)
		{
			meshPipeline->cleanUp();
			meshPipeline->cleanUpShader();
		}
		else if (meshPipeline)
		{
			// Even if not initialized, StaticMeshPipeline constructor creates a command pool
			// that needs to be cleaned up before device destruction
			meshPipeline->cleanUp();
		}

		iblRenderer->cleanUp();
		clearColor->cleanUp();
	}

	void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
	{
		clearColor->recordCommandBuffer(commandBuffer, imageIndex);
		iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

		if (meshPipelineInitialized && !currentMeshDrawList.empty())
		{
			meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, currentMeshDrawList, currentFrustum);
		}

		// Billboard rendering (after mesh pass for proper depth testing)
		if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
		{
			billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
		}
	}

}
