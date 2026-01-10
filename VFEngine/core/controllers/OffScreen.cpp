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

	void OffScreen::rebuildBVH()
	{
		offScreenController->rebuildBVH();
	}

	void OffScreen::markBVHDirty()
	{
		offScreenController->markBVHDirty();
	}

	void OffScreen::createCamera(CameraId id, bool enableOcclusion)
	{
		offScreenController->createCamera(id, enableOcclusion);
	}

	void OffScreen::removeCamera(CameraId id)
	{
		offScreenController->removeCamera(id);
	}

	void OffScreen::setActiveCamera(CameraId id)
	{
		offScreenController->setActiveCamera(id);
	}

	CameraId OffScreen::getActiveCameraId() const
	{
		return offScreenController->getActiveCameraId();
	}

	void OffScreen::prepareFrameCameraFrustums()
	{
		offScreenController->prepareFrameCameraFrustums();
	}

	void OffScreen::prepareFrameAudioSpheres()
	{
		offScreenController->prepareFrameAudioSpheres();
	}

	void OffScreen::prepareFrameBillboards()
	{
		offScreenController->prepareFrameBillboards();
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

	void OffScreen::setPlayMode(bool playMode)
	{
		offScreenController->setPlayMode(playMode);
	}

	bool OffScreen::isPlayMode() const
	{
		return offScreenController->isPlayMode();
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
}
