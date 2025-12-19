#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
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
		, debugRenderer{ std::make_unique<DebugRenderer>(device, swapChain) }
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

	void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
	{
		currentMeshDrawList = std::move(meshes);

		// Check if any meshes have showBoundingBox enabled for debug rendering
		if (debugRendererInitialized && debugRenderer)
		{
			bool hasBoundingBoxes = false;
			for (const auto& mesh : currentMeshDrawList)
			{
				if (mesh.showBoundingBox)
				{
					hasBoundingBoxes = true;
					break;
				}
			}
			debugRenderer->setHasBoundingBoxes(hasBoundingBoxes);
		}
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

	void RenderPassHandler::setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards)
	{
		currentBillboardDrawList = std::move(billboards);
		if (billboardPipelineInitialized && billboardPipeline)
		{
			billboardPipeline->setBillboardList(currentBillboardDrawList);
		}
	}

	void RenderPassHandler::initDebugRenderer()
	{
		if (debugRendererInitialized)
		{
			return;
		}

		// Debug renderer needs mesh pipeline's render pass for proper depth testing
		if (!meshPipelineInitialized)
		{
			return;
		}

		debugRenderer->init(meshPipeline->getRenderPass());
		debugRendererInitialized = true;
	}

	void RenderPassHandler::setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums)
	{
		if (debugRenderer)
		{
			debugRenderer->setCameraFrustumDrawList(std::move(frustums));
		}
	}

	void RenderPassHandler::setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
	{
		currentView = view;
		currentProjection = projection;
	}

	void RenderPassHandler::recreate() const
	{
		iblRenderer->recreate();
		clearColor->recreate();

		if (meshPipelineInitialized)
		{
			meshPipeline->recreate();
		}

		if (debugRendererInitialized)
		{
			debugRenderer->recreate(meshPipeline->getRenderPass());
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

		if (debugRendererInitialized)
		{
			debugRenderer->cleanUp();
			debugRenderer->cleanUpShaders();
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

		// Determine if we need to run the mesh render pass (for meshes or debug rendering)
		bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
		bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasDebugItems);

		if (needsMeshPass)
		{
			render::DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;
			meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, currentMeshDrawList, currentFrustum,
				debugRendererPtr, currentView, currentProjection);
		}

		// Billboard rendering (after mesh pass for proper depth testing)
		if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
		{
			billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
		}
	}

}
