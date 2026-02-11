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

	void OffScreen::setTerrainFrustumCullingEnabled(bool enabled)
	{
		offScreenController->setTerrainFrustumCullingEnabled(enabled);
	}

	void OffScreen::setTerrainMeshletCullingEnabled(bool enabled)
	{
		offScreenController->setTerrainMeshletCullingEnabled(enabled);
	}

	void OffScreen::setTerrainRenderingEnabled(bool enabled)
	{
		offScreenController->setTerrainRenderingEnabled(enabled);
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
}
