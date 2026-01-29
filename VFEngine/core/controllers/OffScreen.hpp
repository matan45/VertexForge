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

		void iblSet(std::string_view iblPath);
		void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
		void iblRemove();

		std::string meshLoad(std::string_view meshPath);
		void meshUnload(const std::string& meshId);
		void meshUpdateCamera(CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
		                      const glm::vec3& cameraPos, float time = 0.0f);
		bool isMeshLoaded(const std::string& meshPath) const;
		std::vector<std::string> getLoadedMeshes() const;
		void prepareCameras();
		std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const;
		void prepareFrameMeshes();

		void rebuildBVH();
		void markBVHDirty();

		void createCamera(CameraId id, bool enableOcclusion = false);
		void removeCamera(CameraId id);
		void setActiveCamera(CameraId id);
		CameraId getActiveCameraId() const;
		void prepareFrameCameraFrustums();
		void prepareFrameAudioSpheres();
		void prepareFrameLightGizmos();

		void prepareFrameBillboards();
		void setShowBillboardIcons(bool show);
		bool getShowBillboardIcons() const;
		bool loadBillboardAtlas(const std::string& atlasPath);

		services::CullingDebugStats getCullingStats() const;

		void applyShadowSettings(const types::RenderSettings& settings);
		services::ShadowStats getShadowStats() const;

		void setPlayMode(bool playMode);
		bool isPlayMode() const;

		void setShowDebugRendering(bool show);
		bool getShowDebugRendering() const;

		void setShowGrid(bool show);
		bool getShowGrid() const;
		void prepareGrid();

		void setShowPhysicsDebug(bool show);
		bool getShowPhysicsDebug() const;
		void prepareFramePhysicsColliders();

		void setViewMode(uint32_t mode);
		uint32_t getViewMode() const;

		void setShowClusterDebug(bool show);
		bool getShowClusterDebug() const;
		void prepareFrameClusterDebug();

		void setShowShadowDebug(bool show);
		bool getShowShadowDebug() const;
		void prepareFrameShadowDebug();

		void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
	};
}
