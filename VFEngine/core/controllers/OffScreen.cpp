#include "OffScreen.hpp"
#include "OffScreenController.hpp"

namespace controllers {
	OffScreen::OffScreen()
	{
		offScreenController = new controllers::OffScreenController();
	}

	OffScreen::~OffScreen()
	{
		delete offScreenController;
	}

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

	void OffScreen::iblAdd(std::string_view iblPath, components::CameraComponent* camera)
	{
		offScreenController->iblAdd(iblPath, camera);
	}

	void OffScreen::iblRemove()
	{
		offScreenController->iblRemove();
	}
}
