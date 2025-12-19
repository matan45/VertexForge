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

	void OffScreen::meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
	                                 const glm::vec3& cameraPos, float time)
	{
		offScreenController->meshUpdateCamera(view, projection, cameraPos, time);
	}

	bool OffScreen::isMeshLoaded(const std::string& meshPath) const
	{
		return offScreenController->isMeshLoaded(meshPath);
	}

	std::vector<std::string> OffScreen::getLoadedMeshes() const
	{
		return offScreenController->getLoadedMeshes();
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
}
