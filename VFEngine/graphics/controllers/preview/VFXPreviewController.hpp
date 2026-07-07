#pragma once

#include "../../core/OffScreen.hpp"
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <vfx/VFXShapeTypes.hpp>
#include <vfx/VFXEventTypes.hpp>
#include <vfx/VFXBurstTypes.hpp>
#include <vfx/VFXBlendMode.hpp>
#include <vfx/VFXOrientationMode.hpp>
#include <vfx/VFXComboTimeline.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <vfx/VFXBundleSignature.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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
        float sizeVariance = 0.0f;
        float lifetimeVariance = 0.0f;
        float speedVariance = 0.0f;
        float rotationVariance = 0.0f;
        float angularVelocityVariance = 0.0f;
        float colorValueVariance = 0.0f;
        float alphaVariance = 0.0f;

        ::vfx::VFXModifierChain modifiers;
        ::vfx::VFXForceChain forces;
        ::vfx::ShapeConfig shape;
        std::vector<::vfx::VFXBurst> bursts;

        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;
        bool flipbookRandomStart = false;
        bool flipbookFrameBlend = false;

        // Rendering
        float alphaClipThreshold = 0.1f;
        ::vfx::VFXBlendMode blendMode = ::vfx::VFXBlendMode::Alpha; // VK-1472 (replaces additiveBlend bool)

        int renderMode = 0;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;

        std::string meshPath;

        // VK-1476: mesh orientation (only used when renderMode == MeshParticle).
        ::vfx::VFXOrientationMode meshOrientationMode = ::vfx::VFXOrientationMode::VelocityForward;
        glm::vec3 meshOrientationAxis{0.0f, 1.0f, 0.0f};
        float meshOrientationSpinRate = 1.0f;

        int maxTrailPoints = 64;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;

        // VK-1474: over-trail width curve + tail gradient (present only when authored).
        ::vfx::VFXCurve ribbonWidthCurve;
        ::vfx::VFXGradient ribbonTailGradient;
        bool hasRibbonWidthCurve = false;
        bool hasRibbonTailGradient = false;

        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;

        ::vfx::VFXEventConfig events;

        // Lighting
        float emissiveIntensity = 1.0f;
        float lightingInfluence = 0.0f;
        int normalMode = 0;
        float ambientAmount = 0.3f;

        bool collisionEnabled = false;
        float collisionBounce = 0.5f;
        float collisionFriction = 0.1f;
        float collisionLifetimeLoss = 0.0f;
    };

    // VK-1451 — one step of a composited sequence preview (controller-side mirror of
    // services::VFXSequencePreviewStep).
    struct VFXSequencePreviewStep
    {
        VFXPreviewParams params;
        glm::mat4 localTransform{1.0f};
        uint32_t seed = 0;
        float startTime = 0.0f;
        float duration = 0.0f;
        bool loop = false;
        int stopMode = 0; // 0 = PlayToCompletion, 1 = StopAfterDuration
        std::string cueName;
    };

    struct VFXSequencePreviewDesc
    {
        std::vector<VFXSequencePreviewStep> steps;
        std::vector<std::pair<float, std::string>> markers;
        uint32_t seed = 0;
        float playbackRate = 1.0f;
        float fixedStep = 0.0f;
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

        // VK-1451 — composited sequence preview. When sequenceMode is on, render()
        // composites every live step bundle into the one offscreen image instead of the
        // single currentParams emitter above. Each bundle owns its own seeded CPU particle
        // system and its own pipeline (one texture/descriptor set per step). Mesh steps share
        // the controller's persistent previewMeshCache (VK-1483) so the 64 MB staging ring +
        // mesh load happen once per preview lifetime, not per seek.
        struct StepBundle
        {
            int stepIndex = -1;
            int mode = 0; // render::vfx::VFXRenderMode (0=Billboard,3=MeshParticle,4=Ribbon)
            glm::mat4 localTransform{1.0f};
            vfx::VFXBundleSignature signature; // VK-1483 — reuse key when parked across seek/loop
            std::unique_ptr<render::vfx::VFXParticleSystem> system;
            std::unique_ptr<render::vfx::VFXBillboardPipeline> billboard;
            std::unique_ptr<render::vfx::VFXMeshPreviewPipeline> mesh;
            std::unique_ptr<render::vfx::VFXRibbonPreviewPipeline> ribbon;
        };

        bool sequenceMode = false;
        bool sequencePlaying = false;
        float sequenceRate = 1.0f;
        float sequenceFixedStep = 0.0f;
        float sequenceAccumulator = 0.0f;
        vfx::VFXSequenceData scheduleData;     // built from the desc; drives the timeline
        vfx::VFXComboTimeline timeline;
        std::vector<VFXSequencePreviewStep> sequenceSteps;
        std::vector<StepBundle> bundles;
        // VK-1483 — dormant step bundles kept alive across seek/loop so their pipelines + shared
        // mesh cache are reused (not rebuilt) when the same step re-spawns. Persistent: emptied
        // only by clearBundles() (setSequence / cleanUp), never by a seek.
        std::unordered_map<int, StepBundle> parkedBundles;
        glm::mat4 lastView{1.0f};
        glm::mat4 lastProjection{1.0f};
        glm::vec3 lastCameraPos{0.0f};
        float lastCameraTime = 0.0f;

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

        // VK-1451 — composited sequence preview transport.
        void setSequence(const VFXSequencePreviewDesc& desc);
        void seekSequence(float seconds);
        void setSequenceRate(float rate);

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void recreateOffscreenResources();
        void createSampler();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        // Sequence-preview internals.
        void configureSystemFromParams(render::vfx::VFXParticleSystem& system, const VFXPreviewParams& params) const;
        void buildStepSystem(StepBundle& bundle, int stepIndex, const VFXSequencePreviewStep& step) const; // VK-1483
        void createBundle(int stepIndex);
        bool tryReuseBundle(int stepIndex, const VFXSequencePreviewStep& step); // VK-1483 — reuse a parked bundle
        void parkBundles();                                // VK-1483 — move live bundles to parkedBundles (no GPU teardown)
        static vfx::VFXBundleSignature signatureFor(const VFXPreviewParams& params); // VK-1483
        void stepSequence(float dt);                       // advance timeline + sim all bundles by dt
        void clearBundles();                               // full teardown of live + parked bundles
        void* renderSequence(uint32_t imageIndex, vk::CommandBuffer commandBuffer);
        void recordBundle(const StepBundle& bundle, vk::CommandBuffer commandBuffer) const;
    };
}
