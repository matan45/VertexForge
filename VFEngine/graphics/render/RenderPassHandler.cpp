#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "billboard/BillboardTypes.hpp"
#include "occlusion/HiZBuffer.hpp"
#include "occlusion/OcclusionCullingManager.hpp"

namespace render {
	RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain, core::OffscreenResources& offscreenResources) : device{ device },
		swapChain{ swapChain }, offscreenResources{ offscreenResources }
		, clearColor{ std::make_unique<ClearColor>(device, swapChain, offscreenResources) }
		, iblRenderer{ std::make_unique<IBL>(device, swapChain, offscreenResources) }
		, meshPipeline{ std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources) }
		, billboardPipeline{ std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources) }
		, hiZBuffer{ std::make_unique<occlusion::HiZBuffer>(device, swapChain) }
		, occlusionCulling{ std::make_unique<occlusion::OcclusionCullingManager>(device, swapChain) }
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

	void RenderPassHandler::initHiZ(vk::Image depthImage, vk::Format depthFormat)
	{
		if (hiZInitialized)
		{
			return;
		}

		hiZBuffer->init(depthImage, offscreenResources.depthImage.depthImageView, depthFormat);
		hiZInitialized = true;
	}

	void RenderPassHandler::initOcclusionCulling()
	{
		if (occlusionCullingInitialized || !hiZInitialized)
		{
			return;
		}

		occlusionCulling->init(hiZBuffer.get());
		occlusionCullingInitialized = true;
	}

	void RenderPassHandler::updateOcclusionObjects(const std::vector<occlusion::GPUObjectData>& objects)
	{
		if (occlusionCullingInitialized)
		{
			occlusionCulling->updateObjects(objects);
		}
	}

	void RenderPassHandler::updateOcclusionCamera(const glm::mat4& viewProj, float nearPlane)
	{
		if (occlusionCullingInitialized)
		{
			occlusionCulling->updateCamera(viewProj, nearPlane);
		}
	}

	std::vector<uint32_t> RenderPassHandler::getOcclusionVisibility()
	{
		if (occlusionCullingInitialized)
		{
			return occlusionCulling->getVisibilityResults();
		}
		return {};
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

		// Hi-Z needs to be recreated when swapchain changes
		// Note: Currently requires re-initialization after resize
	}

	void RenderPassHandler::cleanUp() const
	{
		if (occlusionCullingInitialized)
		{
			occlusionCulling->cleanup();
		}

		if (hiZInitialized)
		{
			hiZBuffer->cleanup();
		}

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

		// Generate Hi-Z pyramid after all depth-writing passes are complete
		// This will be used for occlusion culling in the next frame
		if (hiZInitialized)
		{
			hiZBuffer->generate(commandBuffer);

			// Run GPU occlusion culling using the freshly generated Hi-Z
			if (occlusionCullingInitialized)
			{
				occlusionCulling->cull(commandBuffer);
			}
		}
	}

}
