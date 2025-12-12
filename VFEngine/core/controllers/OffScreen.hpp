#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <string>
#include <vector>

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

		// IBL API
		void iblSet(std::string_view iblPath);
		void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
		void iblRemove();

		// Mesh API
		std::string meshLoad(std::string_view meshPath);
		void meshUnload(const std::string& meshId);
		void meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
		                      const glm::vec3& cameraPos);
		bool isMeshLoaded(const std::string& meshPath) const;
		std::vector<std::string> getLoadedMeshes() const;
		void prepareFrameMeshes();
	};
}
