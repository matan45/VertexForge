#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>

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
		
		// Initialize IBL with HDR path only
		void iblSet(std::string_view iblPath);
		
		// Update camera matrices for IBL rendering
		void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
		
		void iblRemove();
	};
}
