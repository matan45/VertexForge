#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
}

namespace imguiPass
{
    class OffScreenViewPort;
}

namespace controllers
{
    // PBR material parameters for preview
    struct PreviewMaterialParams {
        glm::vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
    };

    class MaterialPreviewController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<imguiPass::OffScreenViewPort> offScreen;

        math::Frustum currentFrustum;
        PreviewMaterialParams materialParams;
        bool initialized = false;
        bool sphereLoaded = false;

        // Internal mesh ID for the procedural sphere
        static constexpr const char* SPHERE_MESH_ID = "__material_preview_sphere__";

    public:
        explicit MaterialPreviewController();
        ~MaterialPreviewController();

        void init();
        void cleanUp();

        // Update material parameters for preview
        void setMaterialParams(const PreviewMaterialParams& params) { materialParams = params; }
        const PreviewMaterialParams& getMaterialParams() const { return materialParams; }

        // Update camera (called each frame before render)
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& cameraPos);

        // Render preview and return ImGui-compatible texture handle
        void* render();

        bool isInitialized() const { return initialized; }
    };
}
