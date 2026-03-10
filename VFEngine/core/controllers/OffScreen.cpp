#include "OffScreen.hpp"
#include "OffScreenController.hpp"

namespace controllers {
	OffScreen::OffScreen()
		: offScreenController{ std::make_unique<controllers::OffScreenController>() }
	{
	}

	OffScreen::~OffScreen() = default;

	void OffScreen::init()
	{
		offScreenController->init();
	}

	void OffScreen::recreate()
	{
		offScreenController->recreate();
	}

	void OffScreen::cleanUp()
	{
		offScreenController->cleanUp();
	}

	void* OffScreen::render()
	{
		return offScreenController->render();
	}

	void* OffScreen::getColorImage(uint32_t imageIndex) const
	{
		return offScreenController->getColorImage(imageIndex);
	}

	void OffScreen::iblSet(std::string_view iblPath)
	{
		offScreenController->iblSet(iblPath);
	}

	void OffScreen::iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
	{
		offScreenController->iblSetCameraMatrices(view, projection);
	}

	void OffScreen::iblRemove()
	{
		offScreenController->iblRemove();
	}

	std::string OffScreen::meshLoad(std::string_view meshPath)
	{
		return offScreenController->meshLoad(meshPath);
	}

	void OffScreen::meshUnload(const std::string& meshId)
	{
		offScreenController->meshUnload(meshId);
	}

	void OffScreen::meshRelease(const std::string& meshPath)
	{
		offScreenController->meshRelease(meshPath);
	}

	void OffScreen::textureRelease(const std::string& texturePath)
	{
		offScreenController->textureRelease(texturePath);
	}

	void OffScreen::materialRelease(const std::string& materialPath)
	{
		offScreenController->materialRelease(materialPath);
	}

	void OffScreen::meshUpdateCamera(CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
	                                 const glm::vec3& cameraPos, float time)
	{
		offScreenController->meshUpdateCamera(cameraId, view, projection, cameraPos, time);
	}

	bool OffScreen::isMeshLoaded(const std::string& meshPath) const
	{
		return offScreenController->isMeshLoaded(meshPath);
	}

	std::vector<std::string> OffScreen::getLoadedMeshes() const
	{
		return offScreenController->getLoadedMeshes();
	}

	std::optional<services::MeshBounds> OffScreen::getMeshBoundingBox(const std::string& meshPath) const
	{
		return offScreenController->getMeshBoundingBox(meshPath);
	}

	void OffScreen::prepareCameras()
	{
		offScreenController->prepareCameras();
	}

	void OffScreen::prepareFrameMeshes()
	{
		offScreenController->prepareFrameMeshes();
	}

	void OffScreen::removeCamera(CameraId id)
	{
		offScreenController->removeCamera(id);
	}

	void OffScreen::prepareFrameCameraFrustums()
	{
		offScreenController->prepareFrameCameraFrustums();
	}

	void OffScreen::prepareFrameAudioSpheres()
	{
		offScreenController->prepareFrameAudioSpheres();
	}

	void OffScreen::prepareFrameLightGizmos()
	{
		offScreenController->prepareFrameLightGizmos();
	}

	void OffScreen::prepareFrameBillboards()
	{
		offScreenController->prepareFrameBillboards();
	}

	void OffScreen::prepareFrameText()
	{
		offScreenController->prepareFrameText();
	}

	void OffScreen::setShowBillboardIcons(bool show)
	{
		offScreenController->setShowBillboardIcons(show);
	}

	bool OffScreen::getShowBillboardIcons() const
	{
		return offScreenController->getShowBillboardIcons();
	}

	bool OffScreen::loadBillboardAtlas(const std::string& atlasPath)
	{
		return offScreenController->loadBillboardAtlas(atlasPath);
	}

	services::CullingDebugStats OffScreen::getCullingStats() const
	{
		return offScreenController->getCullingStats();
	}

	void OffScreen::applyShadowSettings(const types::RenderSettings& settings)
	{
		offScreenController->applyShadowSettings(settings);
	}

	services::ShadowStats OffScreen::getShadowStats() const
	{
		return offScreenController->getShadowStats();
	}

	void OffScreen::setPlayMode(bool playMode)
	{
		offScreenController->setPlayMode(playMode);
	}

	void OffScreen::setShowDebugRendering(bool show)
	{
		offScreenController->setShowDebugRendering(show);
	}

	bool OffScreen::getShowDebugRendering() const
	{
		return offScreenController->getShowDebugRendering();
	}

	void OffScreen::setShowGrid(bool show)
	{
		offScreenController->setShowGrid(show);
	}

	bool OffScreen::getShowGrid() const
	{
		return offScreenController->getShowGrid();
	}

	void OffScreen::prepareGrid()
	{
		offScreenController->prepareGrid();
	}

	void OffScreen::setShowPhysicsDebug(bool show)
	{
		offScreenController->setShowPhysicsDebug(show);
	}

	bool OffScreen::getShowPhysicsDebug() const
	{
		return offScreenController->getShowPhysicsDebug();
	}

	void OffScreen::prepareFramePhysicsColliders()
	{
		offScreenController->prepareFramePhysicsColliders();
	}

	void OffScreen::setViewMode(uint32_t mode)
	{
		offScreenController->setViewMode(mode);
	}

	uint32_t OffScreen::getViewMode() const
	{
		return offScreenController->getViewMode();
	}

	void OffScreen::setShowClusterDebug(bool show)
	{
		offScreenController->setShowClusterDebug(show);
	}

	bool OffScreen::getShowClusterDebug() const
	{
		return offScreenController->getShowClusterDebug();
	}

	void OffScreen::prepareFrameClusterDebug()
	{
		offScreenController->prepareFrameClusterDebug();
	}

	void OffScreen::setShowShadowDebug(bool show)
	{
		offScreenController->setShowShadowDebug(show);
	}

	bool OffScreen::getShowShadowDebug() const
	{
		return offScreenController->getShowShadowDebug();
	}

	void OffScreen::prepareFrameShadowDebug()
	{
		offScreenController->prepareFrameShadowDebug();
	}

	void OffScreen::setShowNavmeshDebug(bool show)
	{
		offScreenController->setShowNavmeshDebug(show);
	}

	bool OffScreen::getShowNavmeshDebug() const
	{
		return offScreenController->getShowNavmeshDebug();
	}

	void OffScreen::updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices,
	                                        const std::vector<uint32_t>& indices)
	{
		offScreenController->updateNavmeshDebugMesh(vertices, indices);
	}

	void OffScreen::clearNavmeshDebugMesh()
	{
		offScreenController->clearNavmeshDebugMesh();
	}

	void OffScreen::updateImmediateDebugDrawList(render::mesh::ImmediateDebugDrawList drawList)
	{
		offScreenController->updateImmediateDebugDrawList(std::move(drawList));
	}

	void OffScreen::prepareFrameUICanvasOutlines()
	{
		offScreenController->prepareFrameUICanvasOutlines();
	}

	void OffScreen::prepareFrameUIImages()
	{
		offScreenController->prepareFrameUIImages();
	}

	void OffScreen::setFrustumCullingEnabled(bool enabled)
	{
		offScreenController->setFrustumCullingEnabled(enabled);
	}

	void OffScreen::setOcclusionCullingEnabled(bool enabled)
	{
		offScreenController->setOcclusionCullingEnabled(enabled);
	}

	void OffScreen::setLODSelectionEnabled(bool enabled)
	{
		offScreenController->setLODSelectionEnabled(enabled);
	}

	void OffScreen::setMeshletFrustumCullingEnabled(bool enabled)
	{
		offScreenController->setMeshletFrustumCullingEnabled(enabled);
	}

	void OffScreen::setMeshletBackfaceCullingEnabled(bool enabled)
	{
		offScreenController->setMeshletBackfaceCullingEnabled(enabled);
	}

	void OffScreen::setDistanceCullingEnabled(bool enabled)
	{
		offScreenController->setDistanceCullingEnabled(enabled);
	}

	void OffScreen::setCategoryDistance(uint32_t category, float distance)
	{
		offScreenController->setCategoryDistance(category, distance);
	}

	void OffScreen::setShadowDistanceMultiplier(float multiplier)
	{
		offScreenController->setShadowDistanceMultiplier(multiplier);
	}

	void OffScreen::setGlobalLodBias(float bias)
	{
		offScreenController->setGlobalLodBias(bias);
	}

	void OffScreen::setTerrainFrustumCullingEnabled(bool enabled)
	{
		offScreenController->setTerrainFrustumCullingEnabled(enabled);
	}

	void OffScreen::setTerrainMeshletCullingEnabled(bool enabled)
	{
		offScreenController->setTerrainMeshletCullingEnabled(enabled);
	}

	void OffScreen::setWBOITEnabled(bool enabled)
	{
		offScreenController->setWBOITEnabled(enabled);
	}

	void OffScreen::setTerrainRenderingEnabled(bool enabled)
	{
		offScreenController->setTerrainRenderingEnabled(enabled);
	}

	void OffScreen::setBillboardRenderingEnabled(bool enabled)
	{
		offScreenController->setBillboardRenderingEnabled(enabled);
	}

	void OffScreen::setTerrainLODBias(float bias)
	{
		offScreenController->setTerrainLODBias(bias);
	}

	void OffScreen::setTerrainErrorThreshold(float threshold)
	{
		offScreenController->setTerrainErrorThreshold(threshold);
	}

	void OffScreen::setTerrainTextureScale(float scale)
	{
		offScreenController->setTerrainTextureScale(scale);
	}

	void OffScreen::setTerrainShadowLOD(uint32_t lod)
	{
		offScreenController->setTerrainShadowLOD(lod);
	}

	void OffScreen::setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize)
	{
		offScreenController->setUIViewportOffset(offset, panelSize);
	}

	void OffScreen::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
	{
		offScreenController->applyPostProcessSettings(settings);
	}

	postprocess::PostProcessSettings OffScreen::getPostProcessSettings() const
	{
		return offScreenController->getPostProcessSettings();
	}

	void OffScreen::setPostProcessEnabled(bool enabled)
	{
		offScreenController->setPostProcessEnabled(enabled);
	}

	bool OffScreen::isPostProcessEnabled() const
	{
		return offScreenController->isPostProcessEnabled();
	}

	void OffScreen::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
	{
		offScreenController->setVFXRuntimeProvider(provider);
	}

	void OffScreen::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
	{
		offScreenController->setTerrainRenderProvider(provider);
	}

	void OffScreen::setWaterRenderProvider(services::IWaterRenderProvider* provider)
	{
		offScreenController->setWaterRenderProvider(provider);
	}

	void OffScreen::setGrassRenderProvider(services::IGrassRenderProvider* provider)
	{
		offScreenController->setGrassRenderProvider(provider);
	}

	void OffScreen::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
	{
		offScreenController->setVegetationRenderProvider(provider);
	}

	void OffScreen::setRaycastCursorUV(const glm::vec2& uv)
	{
		offScreenController->setRaycastCursorUV(uv);
	}

	void OffScreen::clearRaycastCursor()
	{
		offScreenController->clearRaycastCursor();
	}

	terrain::TerrainHitResult OffScreen::getTerrainHitResult() const
	{
		return offScreenController->getTerrainHitResult();
	}

	void OffScreen::setBrushOverlayParams(float radius, float falloff, float shape)
	{
		offScreenController->setBrushOverlayParams(radius, falloff, shape);
	}

	bool OffScreen::applyBrushGPU(
		std::vector<float>& heightData,
		const terrain::BrushGPUParams& params)
	{
		return offScreenController->applyBrushGPU(heightData, params);
	}

	render::RenderPassHandler* OffScreen::getRenderPassHandler() const
	{
		return offScreenController ? offScreenController->getRenderPassHandler() : nullptr;
	}

	plugin::RenderHookHandle OffScreen::registerRenderHook(
		plugin::RenderPassHookPoint hookPoint,
		plugin::RenderHookCallback callback)
	{
		if (!offScreenController) return {};
		return offScreenController->registerRenderHook(hookPoint, std::move(callback));
	}

	void OffScreen::unregisterRenderHook(plugin::RenderHookHandle handle)
	{
		if (offScreenController)
		{
			offScreenController->unregisterRenderHook(handle);
		}
	}

	void OffScreen::addTerrainFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
	{
		if (offScreenController)
		{
			offScreenController->addTerrainFrustum(viewProjection, cameraPos);
		}
	}

	void OffScreen::clearAdditionalTerrainFrustums()
	{
		if (offScreenController)
		{
			offScreenController->clearAdditionalTerrainFrustums();
		}
	}

	void OffScreen::addWaterFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
	{
		if (offScreenController)
		{
			offScreenController->addWaterFrustum(viewProjection, cameraPos);
		}
	}

	void OffScreen::clearAdditionalWaterFrustums()
	{
		if (offScreenController)
		{
			offScreenController->clearAdditionalWaterFrustums();
		}
	}

	// ── GI Settings ──────────────────────────────────────────

	void OffScreen::applyGISettings(const render::gi::GISettings& settings)
	{
		offScreenController->applyGISettings(settings);
	}

	render::gi::GISettings OffScreen::getGISettings() const
	{
		return offScreenController->getGISettings();
	}

	render::gi::GIDebugStats OffScreen::getGIDebugStats() const
	{
		return offScreenController->getGIDebugStats();
	}

	void OffScreen::setGIShowProbes(bool show)
	{
		offScreenController->setGIShowProbes(show);
	}

	void OffScreen::setGIShowCascadeBounds(bool show)
	{
		offScreenController->setGIShowCascadeBounds(show);
	}

	void OffScreen::setGIShowProbeValidity(bool show)
	{
		offScreenController->setGIShowProbeValidity(show);
	}

	// ── Light Streaming Settings ──────────────────────────────

	void OffScreen::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
	{
		offScreenController->setLightStreamingConfig(config);
	}

	render::lighting::LightStreamingConfig OffScreen::getLightStreamingConfig() const
	{
		return offScreenController->getLightStreamingConfig();
	}

	render::lighting::LightStreamingStats OffScreen::getLightStreamingStats() const
	{
		return offScreenController->getLightStreamingStats();
	}

	void OffScreen::registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds)
	{
		offScreenController->registerSectorLights(sectorId, lightEntityIds);
	}

	void OffScreen::unregisterSectorLights(uint32_t sectorId)
	{
		offScreenController->unregisterSectorLights(sectorId);
	}

}
