#pragma once

#include "../core/OffScreen.hpp"
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render::vfx
{
    class VFXBillboardPipeline;
    class VFXMeshPreviewPipeline;
    class VFXRibbonPreviewPipeline;
    class VFXParticleSystem;
}

namespace render::mesh
{
    class MeshGPUCache;
}

namespace controllers
{
    struct VFXPreviewParams
    {
        float spawnRate = 10.0f;
        float lifetime = 2.0f;
        float startSize = 1.0f;
        float startSpeed = 1.0f;
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};
        std::string texturePath;
        bool looping = true;

        ::vfx::VFXModifierChain modifiers;
        ::vfx::VFXForceChain forces;
        ::vfx::ShapeConfig shape;

        // Flipbook (VK-493)
        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;

        // Rendering
        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;

        // Render mode & soft particles (VK-494)
        int renderMode = 0;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;

        // Mesh particle (VK-496)
        std::string meshPath;

        // Ribbon (VK-624)
        int maxTrailPoints = 64;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;

        // UV Scrolling (VK-623)
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
    };

    class VFXPreviewController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<core::CommandPool> commandPool;
        std::unique_ptr<render::vfx::VFXBillboardPipeline> pipeline;
        std::unique_ptr<render::vfx::VFXMeshPreviewPipeline> meshPipeline;
        std::unique_ptr<render::vfx::VFXRibbonPreviewPipeline> ribbonPipeline;
        std::unique_ptr<render::mesh::MeshGPUCache> previewMeshCache;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;

        core::OffscreenResources offscreenResources;
        vk::Sampler sampler;
        std::vector<vk::Fence> inFlightFences;

        VFXPreviewParams currentParams;
        bool initialized = false;
        vk::Extent2D lastExtent{};

    public:
        explicit VFXPreviewController();
        ~VFXPreviewController();

        void init();
        void cleanUp();

        void setParams(const VFXPreviewParams& params);
        const VFXPreviewParams& getParams() const { return currentParams; }

        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPos, float time);
        void update(float deltaTime);

        void play();
        void pause();
        void stop();
        bool isPlaying() const;

        void* render();
        bool isInitialized() const { return initialized; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void recreateOffscreenResources();
        void createSampler();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
    };
}
