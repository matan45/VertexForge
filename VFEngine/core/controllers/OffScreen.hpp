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
#include "terrain/TerrainHitResult.hpp"
#include "terrain/BrushTypes.hpp"
#include "postprocess/PostProcessTypes.hpp"

namespace services
{
	class IVFXRuntimeProvider;
	class ITerrainRenderProvider;
	class IWaterRenderProvider;
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

		void removeCamera(CameraId id);
		void prepareFrameCameraFrustums();
		void prepareFrameAudioSpheres();
		void prepareFrameLightGizmos();

		void prepareFrameBillboards();
		void prepareFrameText();
		void setShowBillboardIcons(bool show);
		bool getShowBillboardIcons() const;
		bool loadBillboardAtlas(const std::string& atlasPath);

		services::CullingDebugStats getCullingStats() const;

		void applyShadowSettings(const types::RenderSettings& settings);
		services::ShadowStats getShadowStats() const;

		void setPlayMode(bool playMode);

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

		void setShowNavmeshDebug(bool show);
		bool getShowNavmeshDebug() const;
		void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices);
		void clearNavmeshDebugMesh();

		void prepareFrameUICanvasOutlines();
		void prepareFrameUIImages();

		void setFrustumCullingEnabled(bool enabled);
		void setOcclusionCullingEnabled(bool enabled);
		void setLODSelectionEnabled(bool enabled);
		void setMeshletFrustumCullingEnabled(bool enabled);
		void setMeshletBackfaceCullingEnabled(bool enabled);
		void setTerrainFrustumCullingEnabled(bool enabled);
		void setTerrainMeshletCullingEnabled(bool enabled);

		void setWBOITEnabled(bool enabled);

		void setTerrainRenderingEnabled(bool enabled);
		void setTerrainLODBias(float bias);
		void setTerrainErrorThreshold(float threshold);
		void setTerrainTextureScale(float scale);
		void setTerrainShadowLOD(uint32_t lod);

		void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize);

		void applyPostProcessSettings(const postprocess::PostProcessSettings& settings);
		postprocess::PostProcessSettings getPostProcessSettings() const;
		void setPostProcessEnabled(bool enabled);
		bool isPostProcessEnabled() const;

		void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
		void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
		void setWaterRenderProvider(services::IWaterRenderProvider* provider);

		void setRaycastCursorUV(const glm::vec2& uv);
		void clearRaycastCursor();
		terrain::TerrainHitResult getTerrainHitResult() const;

		void setBrushOverlayParams(float radius, float falloff, float shape);

		bool applyBrushGPU(
			std::vector<float>& heightData,
			const terrain::BrushGPUParams& params);
	};
}
