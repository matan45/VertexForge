#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "material/MaterialTypes.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <memory>
#include <string>
#include <vector>


namespace core
{
    class Device;
    class SwapChain;
}

namespace render::preview
{
    class PreviewViewPort;
}

namespace controllers
{
    struct PreviewTextureGPU
    {
        vk::Image image;
        core::VulkanAllocation allocation;
        vk::ImageView imageView;
        vk::Sampler sampler;
        bool valid = false;
    };

    struct TextureManagerImpl
    {
        static constexpr int MAX_TEXTURES = 16;
        std::unordered_map<std::string, PreviewTextureGPU> textureCache;
        std::array<std::string, MAX_TEXTURES> textureSlots;
        PreviewTextureGPU defaultTexture;
        bool texturesNeedUpdate = false;

        explicit TextureManagerImpl()
        {
            for (auto& slot : textureSlots)
            {
                slot.clear();
            }
        }
    };
    
    struct PreviewMaterialParams
    {
        glm::vec4 albedo{0.8f, 0.8f, 0.8f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Texture paths (empty = use scalar value)
        // Order matches material::TextureSlot enum
        std::string albedoTexturePath;
        std::string normalTexturePath;
        std::string ormTexturePath;          // ORM packed texture (R=AO, G=Roughness, B=Metallic)
        std::string metallicTexturePath;     // Legacy individual
        std::string roughnessTexturePath;    // Legacy individual
        std::string aoTexturePath;           // Legacy individual
        std::string emissionTexturePath;
        std::string heightTexturePath;
        
        std::string materialPath;

        // Flag to enable custom shader pipeline (only after explicit compile)
        bool useCustomShader = false;

        // Material graph for dynamic evaluation (Time, Sin, Cos nodes)
        std::shared_ptr<material::MaterialData> materialData;
    };

    class MaterialPreviewController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<render::preview::PreviewViewPort> offScreen;

        math::Frustum currentFrustum;
        PreviewMaterialParams materialParams;
        bool initialized = false;
        bool sphereLoaded = false;
        float currentTime = 0.0f;

        // Internal mesh ID for the procedural sphere
        static constexpr const char* SPHERE_MESH_ID = "__material_preview_sphere__";

        // Texture management (implementation details hidden via pImpl pattern)
        std::unique_ptr<TextureManagerImpl> textureManager;

        // Deferred descriptor update state
        bool pendingDescriptorUpdate = false;
        std::vector<bool> pendingDescriptorUpdateFrames;

        // Update texture descriptors (called when safe after fence wait)
        void updateTextureDescriptorsIfPending(uint32_t imageIndex);

    public:
        explicit MaterialPreviewController();
        ~MaterialPreviewController();

        void init();
        void cleanUp();
        
        void setMaterialParams(const PreviewMaterialParams& params);
        const PreviewMaterialParams& getMaterialParams() const { return materialParams; }

        // Get texture slot index for a path (-1 if not loaded)
        int getTextureSlot(const std::string& path) const;
        
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPos, float time = 0.0f);
        
        void* render();

        // VK-1379: copy the last rendered frame into an owned `size` px thumbnail.
        void* snapshot(uint32_t size);
        void releaseSnapshot(void* handle);

        // Get last shader compilation error (for UI display)
        std::string getLastShaderCompilationError() const;

        bool isInitialized() const { return initialized; }

    private:
        void loadAndAssignTextures(const std::array<std::string, TextureManagerImpl::MAX_TEXTURES>& texturePaths);

        static std::optional<float> evaluateFloatValue(const material::ShaderGraph& graph, uint32_t nodeId, float time);
        static std::optional<float> getInputFloat(const material::ShaderGraph& graph, uint32_t nodeId, const std::string& pinName, float time);
        static float evaluateEmissionStrength(const material::ShaderGraph& graph, float time);
    };
}
