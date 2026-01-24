#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "providers/IOffScreenProvider.hpp"
#include "types/CameraTypes.hpp"

namespace services
{
	class IVFXRuntimeProvider;
}

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
		void prepareFrameAudioSpheres();
		void prepareFrameLightGizmos();

		// Billboard API
		void prepareFrameBillboards();
		void setShowBillboardIcons(bool show);
		bool getShowBillboardIcons() const;
		bool loadBillboardAtlas(const std::string& atlasPath);

		// Debug/Stats API
		services::CullingDebugStats getCullingStats() const;

		// Editor Mode API
		void setPlayMode(bool playMode);
		bool isPlayMode() const;

		// Debug Rendering API
		void setShowDebugRendering(bool show);
		bool getShowDebugRendering() const;

		// Grid API
		void setShowGrid(bool show);
		bool getShowGrid() const;
		void prepareGrid();

		// Physics Debug API
		void setShowPhysicsDebug(bool show);
		bool getShowPhysicsDebug() const;
		void prepareFramePhysicsColliders();

		// View Mode API
		void setViewMode(uint32_t mode);
		uint32_t getViewMode() const;

		// Cluster Debug API
		void setShowClusterDebug(bool show);
		bool getShowClusterDebug() const;
		void prepareFrameClusterDebug();

		// VFX Runtime API
		void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
	};
}
