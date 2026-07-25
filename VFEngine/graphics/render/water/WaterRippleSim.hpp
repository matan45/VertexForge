#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../../utilities/water/RippleSimMath.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <array>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::water
{
    // Per-frame tuning, published from GPUDrivenRenderer::updateWater on the render thread.
    struct RippleSimParams
    {
        glm::vec2 origin{0.0f};                                     // texel-snapped patch min corner
        float patchSize = ::water::RIPPLE_DEFAULT_PATCH_SIZE;
        float waveSpeed = 3.0f;                                     // m/s, clamped to CFL internally
        float damping = 1.0f;                                       // per-second velocity decay
        float foamGain = 0.05f;
        float foamDecay = 1.5f;                                     // per-second foam decay
        bool enabled = false;
    };

    // std430 twin of `struct WaterImpulse` in ripple_sim.glsl. vec2 + 2 floats packs to 16 bytes
    // with no padding in either language, so the arrays are binary-identical.
    struct GPUWaterImpulse
    {
        glm::vec2 positionXZ{0.0f};
        float radius = 0.0f;
        float strength = 0.0f;
    };
    static_assert(sizeof(GPUWaterImpulse) == 16);

    struct RippleSimPushConstants
    {
        glm::vec2 originXZ{0.0f};       //  0
        glm::vec2 prevOriginXZ{0.0f};   //  8
        float patchSize = 1.0f;         // 16
        float dt = 0.0f;                // 20
        float waveSpeed = 0.0f;         // 24
        float damping = 1.0f;           // 28  per-STEP multiplier, not the per-second rate
        uint32_t impulseCount = 0;      // 32
        float foamGain = 0.0f;          // 36
        float foamDecay = 1.0f;         // 40  per-STEP multiplier
        uint32_t resolution = 0;        // 44
    };
    static_assert(sizeof(RippleSimPushConstants) == 48);

    // VK-1606: the GPU side of water::RippleSimMath - a camera-following patch of damped-wave-equation
    // water that wakes, splashes and scripted impulses write into.
    //
    // Resolution is a compile-time constant, so every image is created exactly once and never
    // recreated. That is deliberate and load-bearing: the output view is written into set 9 binding 4
    // (and into the refraction dummy set that RTT views bind) once at init, and can therefore never
    // go stale - the same reasoning as WaterShoreDepthResources.
    //
    // The ping-pong state pair is INTERNAL and never reaches set 9. Only `outputImage` does, which is
    // what makes a per-frame parity flip possible without rewriting a descriptor every frame.
    class WaterRippleSim
    {
    public:
        explicit WaterRippleSim(core::Device& device);
        ~WaterRippleSim();

        WaterRippleSim(const WaterRippleSim&) = delete;
        WaterRippleSim& operator=(const WaterRippleSim&) = delete;

        void init();
        void cleanup();

        // Render thread. Appends to the impulses consumed by the next dispatch; anything past
        // MAX_WATER_IMPULSES in a single frame is dropped (the newest wins - a fresh splash matters
        // more than one already a frame old).
        void queueImpulses(const std::vector<::water::WaterImpulse>& impulses);

        // VK-1607 review: NOT a plain setter. A change to patchSize invalidates the whole ping-pong
        // state (see ::water::rippleNeedsReset), so this arms the reset the next dispatch consumes.
        void setParams(const RippleSimParams& newParams);
        [[nodiscard]] const RippleSimParams& getParams() const { return params; }

        // Records the impulse upload, the fixed sub-steps and the output barrier. MUST be called
        // outside a render pass; it sits next to dispatchOceanFFT and uploadShoreDepthField.
        // `time` is the same clock that fills CameraUBO::u_Time.
        void dispatch(vk::CommandBuffer cmd, float time);

        [[nodiscard]] vk::ImageView getOutputView() const { return outputView; }
        [[nodiscard]] vk::Sampler getSampler() const { return sampler; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // True once at least one step has run, i.e. the output image holds simulated water rather
        // than the zero-fill from init. Gates WATER_FLAG_RIPPLES.
        [[nodiscard]] bool hasSimulated() const { return simulated; }

    private:
        void createImages();
        void createSampler();
        void createImpulseBuffer();
        void createDescriptors();
        void createPipeline();
        void transitionImagesInitial();

        void recordImpulseUpload(vk::CommandBuffer cmd);
        static void insertStateBarrier(vk::CommandBuffer cmd);

        core::Device& device;

        // state[p]: (height, velocity, foam, unused). Always in eGeneral - it is only ever touched
        // by the compute stage.
        std::array<vk::Image, 2> stateImages{};
        std::array<core::VulkanAllocation, 2> stateAllocations{};
        std::array<vk::ImageView, 2> stateViews{};

        // (height, normalX, normalZ, foam). Sampled by the water vertex AND fragment stages.
        vk::Image outputImage;
        core::VulkanAllocation outputAllocation;
        vk::ImageView outputView;
        vk::Sampler sampler;

        vk::Buffer impulseBuffer;
        core::VulkanAllocation impulseAllocation;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSetLayout descriptorSetLayout;
        std::array<vk::DescriptorSet, 2> descriptorSets{};

        std::unique_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        RippleSimParams params;
        std::vector<GPUWaterImpulse> pendingImpulses;

        glm::vec2 previousOrigin{0.0f};
        float lastDispatchTime = -1.0f;
        float stepAccumulator = 0.0f;
        uint32_t parity = 0;
        // Forces the next step to treat the whole patch as freshly scrolled in, which zeroes it for
        // free (see dispatch). Set on init and whenever the sim is switched off, so re-enabling can
        // never pop a stale field back onto the water.
        bool needsReset = true;
        bool simulated = false;
        bool initialized = false;
    };
}
