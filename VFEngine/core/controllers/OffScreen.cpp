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

	void* OffScreen::render(const std::function<void()>& preRenderCallback)
	{
		return offScreenController->render(preRenderCallback);
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

	void OffScreen::prepareSceneData()
	{
		offScreenController->prepareSceneData();
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

	types::RTShadowStats OffScreen::getRTShadowStats() const
	{
		return offScreenController->getRTShadowStats();
	}

	services::GPUPipelineStatus OffScreen::getGPUPipelineStatus() const
	{
		return offScreenController->getGPUPipelineStatus();
	}

	void OffScreen::setPlayMode(bool playMode)
	{
		offScreenController->setPlayMode(playMode);
	}

	void OffScreen::waitForIdle()
	{
		offScreenController->waitForIdle();
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

	void OffScreen::setShowWireframe(bool show)
	{
		offScreenController->setShowWireframe(show);
	}

	bool OffScreen::getShowWireframe() const
	{
		return offScreenController->getShowWireframe();
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

	void OffScreen::setLODCrossfadeEnabled(bool enabled)
	{
		offScreenController->setLODCrossfadeEnabled(enabled);
	}

	void OffScreen::setMeshletFrustumCullingEnabled(bool enabled)
	{
		offScreenController->setMeshletFrustumCullingEnabled(enabled);
	}

	void OffScreen::setMeshletBackfaceCullingEnabled(bool enabled)
	{
		offScreenController->setMeshletBackfaceCullingEnabled(enabled);
	}

	void OffScreen::setMeshletOcclusionCullingEnabled(bool enabled)
	{
		offScreenController->setMeshletOcclusionCullingEnabled(enabled);
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

	void OffScreen::setDecalRenderingEnabled(bool enabled)
	{
		offScreenController->setDecalRenderingEnabled(enabled);
	}

	void OffScreen::setDecalDrawList(const std::vector<services::DecalRenderData>& decals)
	{
		offScreenController->setDecalDrawList(decals);
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

	void OffScreen::setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize)
	{
		offScreenController->setUIViewportOffset(offset, panelSize);
	}

}
