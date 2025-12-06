#pragma once
#include <memory>
#include "components/Components.hpp"

namespace controllers {

	class OffScreenController;

	class OffScreen
	{
	private:
		std::unique_ptr<controllers::OffScreenController> offScreenController;

	public:
		explicit OffScreen();
		~OffScreen();

		void init();
		void cleanUp();

		void* render();
		void iblAdd(std::string_view iblPath, components::CameraComponent* camera);
		void iblRemove();
	};
}

