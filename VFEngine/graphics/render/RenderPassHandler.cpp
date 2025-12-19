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
#include "occlusion/CameraRenderData.hpp"

namespace render {
	RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain, core::OffscreenResources& offscreenResources) : device{ device },
		swapChain{ swapChain }, offscreenResources{ offscreenResources }
		, clearColor{ std::make_unique<ClearColor>(device, swapChain, offscreenResources) }
		, iblRenderer{ std::make_unique<IBL>(device, swapChain, offscreenResources) }
		, meshPipeline{ std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources) }
		, billboardPipeline{ std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources) }
		, cameraOcclusionManager{ std::make_unique<occlusion::CameraOcclusionManager>(device, swapChain) }
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

	// Camera management
	occlusion::CameraRenderData* RenderPassHandler::createCamera(occlusion::CameraId id, bool enableOcclusion)
	{
		return cameraOcclusionManager->createCamera(id, enableOcclusion);
	}

	occlusion::CameraRenderData* RenderPassHandler::getCamera(occlusion::CameraId id)
	{
		return cameraOcclusionManager->getCamera(id);
	}

	void RenderPassHandler::removeCamera(occlusion::CameraId id)
	{
		cameraOcclusionManager->removeCamera(id);
	}

	void RenderPassHandler::setActiveCamera(occlusion::CameraId id)
	{
		cameraOcclusionManager->setActiveCamera(id);
	}

	occlusion::CameraId RenderPassHandler::getActiveCameraId() const
	{
		return cameraOcclusionManager->getActiveCameraId();
	}

	// Hi-Z methods - Main camera (backward compatible)
	void RenderPassHandler::initHiZ(vk::Image depthImage, vk::Format depthFormat)
	{
		initHiZ(occlusion::MAIN_CAMERA_ID, depthImage, offscreenResources.depthImage.depthImageView, depthFormat);
	}

	void RenderPassHandler::initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView, vk::Format depthFormat)
	{
		cameraOcclusionManager->initCameraHiZ(cameraId, depthImage, depthView, depthFormat);
	}

	bool RenderPassHandler::isHiZInitialized() const
	{
		return isHiZInitialized(occlusion::MAIN_CAMERA_ID);
	}

	bool RenderPassHandler::isHiZInitialized(occlusion::CameraId cameraId) const
	{
		return cameraOcclusionManager->isHiZInitialized(cameraId);
	}

	// Occlusion culling methods - Main camera (backward compatible)
	void RenderPassHandler::initOcclusionCulling()
	{
		initOcclusionCulling(occlusion::MAIN_CAMERA_ID);
	}

	void RenderPassHandler::initOcclusionCulling(occlusion::CameraId cameraId)
	{
		cameraOcclusionManager->initCameraOcclusionCulling(cameraId);
	}

	void RenderPassHandler::updateOcclusionObjects(const std::vector<occlusion::GPUObjectData>& objects)
	{
		updateOcclusionObjects(occlusion::MAIN_CAMERA_ID, objects);
	}

	void RenderPassHandler::updateOcclusionObjects(occlusion::CameraId cameraId, const std::vector<occlusion::GPUObjectData>& objects)
	{
		cameraOcclusionManager->updateOcclusionObjects(cameraId, objects);
	}

	void RenderPassHandler::updateOcclusionCamera(const glm::mat4& viewProj, float nearPlane)
	{
		updateOcclusionCamera(occlusion::MAIN_CAMERA_ID, viewProj, nearPlane);
	}

	void RenderPassHandler::updateOcclusionCamera(occlusion::CameraId cameraId, const glm::mat4& viewProj, float nearPlane)
	{
		cameraOcclusionManager->updateCamera(cameraId, viewProj, nearPlane);
	}

	std::vector<uint32_t> RenderPassHandler::getOcclusionVisibility()
	{
		return getOcclusionVisibility(occlusion::MAIN_CAMERA_ID);
	}

	std::vector<uint32_t> RenderPassHandler::getOcclusionVisibility(occlusion::CameraId cameraId)
	{
		return cameraOcclusionManager->getVisibilityResults(cameraId);
	}

	bool RenderPassHandler::isOcclusionCullingInitialized() const
	{
		return isOcclusionCullingInitialized(occlusion::MAIN_CAMERA_ID);
	}

	bool RenderPassHandler::isOcclusionCullingInitialized(occlusion::CameraId cameraId) const
	{
		return cameraOcclusionManager->isOcclusionInitialized(cameraId);
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

		// Hi-Z needs to be recreated when swapchain changes
		// Note: Currently requires re-initialization after resize
	}

	void RenderPassHandler::cleanUp() const
	{
		if (cameraOcclusionManager)
		{
			cameraOcclusionManager->cleanup();
		}

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

		// Generate Hi-Z pyramid and run occlusion culling for the active camera
		occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

		if (cameraOcclusionManager->isHiZInitialized(activeCameraId))
		{
			cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

			if (cameraOcclusionManager->isOcclusionInitialized(activeCameraId))
			{
				cameraOcclusionManager->runOcclusionCulling(activeCameraId, commandBuffer);
			}
		}
	}

}
