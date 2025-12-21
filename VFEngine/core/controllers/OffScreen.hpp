#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "../../services/providers/IOffScreenProvider.hpp"
#include "../../utilities/types/CameraTypes.hpp"

namespace controllers {

	using types::CameraId;
	using types::MAIN_CAMERA_ID;

	class OffScreenController;

	class OffScreen
	{
	private:
		std::unique_ptr<controllers::OffScreenController> offScreenController;

	public:
		explicit OffScreen();
		~OffScreen();

		void init();
		void recreate();
		void cleanUp();

		void* render();

		// IBL API
		void iblSet(std::string_view iblPath);
		void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
		void iblRemove();

		// Mesh API
		std::string meshLoad(std::string_view meshPath);
		void meshUnload(const std::string& meshId);
		void meshUpdateCamera(CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
		                      const glm::vec3& cameraPos, float time = 0.0f);
		bool isMeshLoaded(const std::string& meshPath) const;
		std::vector<std::string> getLoadedMeshes() const;
		void prepareCameras();  // Sync CameraComponents with occlusion system
		std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;
		void prepareFrameMeshes();

		// BVH spatial culling
		void rebuildBVH();
		void markBVHDirty();

		// Multi-camera occlusion culling
		void createCamera(CameraId id, bool enableOcclusion = false);
		void removeCamera(CameraId id);
		void setActiveCamera(CameraId id);
		CameraId getActiveCameraId() const;
		void prepareFrameCameraFrustums();

		// Billboard API
		void prepareFrameBillboards();
		void setShowBillboardIcons(bool show);
		bool getShowBillboardIcons() const;
		bool loadBillboardAtlas(const std::string& atlasPath);

		// Debug/Stats API
		services::CullingDebugStats getCullingStats() const;
	};
}
