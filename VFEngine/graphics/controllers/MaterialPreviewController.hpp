#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "material/MaterialTypes.hpp"
#include <memory>
#include <string>
#include <array>
#include <unordered_map>

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

        // Texture paths (empty = use scalar value)
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;

        // Material graph for dynamic evaluation (Time, Sin, Cos nodes)
        std::shared_ptr<material::MaterialData> materialData;
    };

    // Forward declaration for internal texture data (defined in cpp)
    struct PreviewTextureGPU;

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
        float currentTime = 0.0f;

        // Internal mesh ID for the procedural sphere
        static constexpr const char* SPHERE_MESH_ID = "__material_preview_sphere__";

        // Texture management (implementation details hidden via pImpl pattern)
        struct TextureManagerImpl;
        std::unique_ptr<TextureManagerImpl> textureManager;

    public:
        explicit MaterialPreviewController();
        ~MaterialPreviewController();

        void init();
        void cleanUp();

        // Update material parameters for preview
        void setMaterialParams(const PreviewMaterialParams& params);
        const PreviewMaterialParams& getMaterialParams() const { return materialParams; }

        // Get texture slot index for a path (-1 if not loaded)
        int getTextureSlot(const std::string& path) const;

        // Update camera (called each frame before render)
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& cameraPos, float time = 0.0f);

        // Render preview and return ImGui-compatible texture handle
        void* render();

        bool isInitialized() const { return initialized; }
    };
}
