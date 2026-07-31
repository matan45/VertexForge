#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    // VK-1616. Mirrors the push_constant block in resources/shaders/terrain/hydraulic_erosion.glsl
    // field for field; there is no codegen between them, so any edit here needs the same edit
    // there. 120 bytes, comfortably under the 128 B guaranteed minimum push-constant size.
    //
    // Deliberately NOT an extension of BrushComputePushConstants: that one is pinned at 96 bytes by
    // a static_assert and shared by all nine brushes that go through brush_compute.glsl. Hydraulic
    // erosion has a different dispatch shape (a multi-tile region, not one tile) and a different
    // buffer set, so it gets its own pipeline rather than putting the working brushes at risk.
    struct HydraulicErosionPushConstants
    {
        glm::vec2 regionOriginWorld;  //   0  world XZ of region cell (0,0)
        glm::vec2 brushCenter;        //   8
        uint32_t regionWidth;         //  16
        uint32_t regionHeight;        //  20
        uint32_t passIndex;           //  24  0=FLUX 1=WATER 2=EROSION 3=ADVECT 4=THERMAL 5=RESOLVE
        uint32_t falloffType;         //  28
        uint32_t shapeType;           //  32
        uint32_t terrainParity;       //  36
        uint32_t sedimentParity;      //  40
        uint32_t cellCount;           //  44  stride between the ping-pong halves
        float cellSize;               //  48
        float brushRadius;            //  52
        float dt;                     //  56
        float rainAmount;             //  60
        float sedimentCapacity;       //  64
        float dissolveRate;           //  68
        float depositRate;            //  72
        float evaporation;            //  76
        float gravity;                //  80
        float minTiltSin;             //  84
        float maxVelocity;            //  88
        float maxStepDelta;           //  92
        float minWater;               //  96
        float smoothing;              // 100
        float talusThreshold;         // 104
        float strengthScale;          // 108
        float minHeight;              // 112
        float maxHeight;              // 116
    };
    static_assert(sizeof(HydraulicErosionPushConstants) == 120,
                  "HydraulicErosionPushConstants must be 120 bytes and match hydraulic_erosion.glsl");

    // Runs the whole iterative solve inside ONE command buffer: the caller already pays a command
    // buffer allocation, a fence, a mutex-guarded submit and a full stall per dab (exactly like
    // BrushComputePipeline::applyBrush), so recording iterations*passes dispatches with memory
    // barriers between them costs nothing beyond the GPU time itself.
    class HydraulicErosionPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // Device-local simulation state. Sizes are per-cell multiples of the region cell count:
        // terrain and sediment are doubled for the ping-pong halves, flux and flow are vec4.
        vk::Buffer heightOrigBuffer;
        core::VulkanAllocation heightOrigAllocation;
        vk::Buffer heightOutBuffer;
        core::VulkanAllocation heightOutAllocation;
        vk::Buffer terrainBuffer;
        core::VulkanAllocation terrainAllocation;
        vk::Buffer waterBuffer;
        core::VulkanAllocation waterAllocation;
        vk::Buffer sedimentBuffer;
        core::VulkanAllocation sedimentAllocation;
        vk::Buffer fluxBuffer;
        core::VulkanAllocation fluxAllocation;
        vk::Buffer flowBuffer;
        core::VulkanAllocation flowAllocation;
        vk::Buffer validBuffer;
        core::VulkanAllocation validAllocation;

        // Host-visible staging.
        vk::Buffer stagingUploadBuffer;
        core::VulkanAllocation stagingUploadAllocation;
        vk::Buffer stagingValidBuffer;
        core::VulkanAllocation stagingValidAllocation;
        vk::Buffer stagingReadbackBuffer;
        core::VulkanAllocation stagingReadbackAllocation;

        vk::CommandPool computeCommandPool;

        bool initialized = false;
        uint32_t currentCellCount = 0;

        static constexpr uint32_t WORKGROUP_SIZE = 8;
        static constexpr uint32_t BINDING_COUNT = 8;

    public:
        explicit HydraulicErosionPipeline(core::Device& device);
        ~HydraulicErosionPipeline();

        HydraulicErosionPipeline(const HydraulicErosionPipeline&) = delete;
        HydraulicErosionPipeline& operator=(const HydraulicErosionPipeline&) = delete;

        void init();
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Synchronous solve over one gathered region. `field` holds the region's heights on entry
        // and receives the eroded heights on return; `validMask` is 1 per cell a tile owns, 0 for a
        // hole in the grid. `constants.passIndex` and the two parity fields are driven internally.
        // Returns true on success.
        bool simulate(std::vector<float>& field,
                      const std::vector<uint32_t>& validMask,
                      HydraulicErosionPushConstants constants,
                      uint32_t iterations,
                      uint32_t thermalInterval);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createCommandPool();
        void ensureCapacity(uint32_t cellCount);
        void destroyBuffers();
        void insertStateBarrier(vk::CommandBuffer cmd) const;
    };
}
