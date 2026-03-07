#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
    class Shader;
}

namespace render::water
{
    struct OceanFFTConfig
    {
        uint32_t resolution = 256;
        float patchSize = 100.0f;
        float windSpeed = 20.0f;
        float windDirection = 45.0f;
        float amplitude = 0.002f;
        float choppiness = 1.5f;
        float gravity = 9.81f;
        float foamThreshold = 0.3f;
    };

    struct SpectrumPushConstants
    {
        uint32_t N;
        float patchSize;
        float windSpeed;
        float windDirX;
        float windDirZ;
        float amplitude;
        float gravity;
        float cutoffLow;
        uint32_t seed;
        uint32_t padding;
    };
    static_assert(sizeof(SpectrumPushConstants) == 40);

    struct TimeEvolvePushConstants
    {
        uint32_t N;
        float time;
        float choppiness;
        float patchSize;
        float gravity;
        uint32_t padding1;
        uint32_t padding2;
        uint32_t padding3;
    };
    static_assert(sizeof(TimeEvolvePushConstants) == 32);

    struct FFTPushConstants
    {
        uint32_t N;
        uint32_t stage;
        uint32_t direction;
        uint32_t padding;
    };
    static_assert(sizeof(FFTPushConstants) == 16);

    struct MergePushConstants
    {
        uint32_t N;
        float choppiness;
        float patchSize;
        float foamThreshold;
    };
    static_assert(sizeof(MergePushConstants) == 16);

    class OceanFFT
    {
    private:
        core::Device& device;
        OceanFFTConfig config;

        // Compute shaders
        std::unique_ptr<core::Shader> spectrumShader;
        std::unique_ptr<core::Shader> timeEvolveShader;
        std::unique_ptr<core::Shader> fftShader;
        std::unique_ptr<core::Shader> mergeShader;

        // Compute pipelines
        vk::Pipeline spectrumPipeline;
        vk::Pipeline timeEvolvePipeline;
        vk::Pipeline fftPipeline;
        vk::Pipeline mergePipeline;

        // Pipeline layouts
        vk::PipelineLayout spectrumPipelineLayout;
        vk::PipelineLayout timeEvolvePipelineLayout;
        vk::PipelineLayout fftPipelineLayout;
        vk::PipelineLayout mergePipelineLayout;

        // Descriptor set layouts
        vk::DescriptorSetLayout spectrumDescLayout;
        vk::DescriptorSetLayout timeEvolveDescLayout;
        vk::DescriptorSetLayout fftDescLayout;
        vk::DescriptorSetLayout mergeDescLayout;
        vk::DescriptorSetLayout oceanTextureDescLayout;

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet spectrumDescSet;
        vk::DescriptorSet timeEvolveDescSet;
        std::array<vk::DescriptorSet, 6> fftDescSets; // 3 fields x 2 ping-pong
        vk::DescriptorSet mergeDescSet;
        vk::DescriptorSet oceanTextureDescSet;

        // h0 spectrum texture (RGBA32F)
        vk::Image h0Image;
        vk::DeviceMemory h0Memory;
        vk::ImageView h0View;

        // Field textures: 3 fields (Dy, Dx, Dz) x 2 ping-pong each (RG32F)
        struct FieldPair
        {
            vk::Image images[2];
            vk::DeviceMemory memory[2];
            vk::ImageView views[2];
        };
        std::array<FieldPair, 3> fields;

        // Output textures (RGBA16F) - sampled by water shader
        vk::Image displacementImage;
        vk::DeviceMemory displacementMemory;
        vk::ImageView displacementView;

        vk::Image normalImage;
        vk::DeviceMemory normalMemory;
        vk::ImageView normalView;

        vk::Sampler outputSampler;

        // GPU readback for CPU-side displacement sampling (physics)
        vk::Buffer readbackBuffer;
        vk::DeviceMemory readbackMemory;
        std::vector<glm::vec4> cpuDisplacementData;
        bool readbackReady = false;

        bool initialized = false;
        bool spectrumDirty = true;
        bool firstDispatch = true;

        static constexpr uint32_t WORKGROUP_SIZE = 16;

    public:
        explicit OceanFFT(core::Device& device);
        ~OceanFFT();

        OceanFFT(const OceanFFT&) = delete;
        OceanFFT& operator=(const OceanFFT&) = delete;

        void init(const OceanFFTConfig& config);
        void cleanup();

        void updateConfig(const OceanFFTConfig& newConfig);

        // Record compute commands into the command buffer
        void dispatch(vk::CommandBuffer cmd, float time);

        // Insert barrier between compute output and graphics sampling
        void insertBarrier(vk::CommandBuffer cmd);

        [[nodiscard]] vk::DescriptorSetLayout getOceanTextureLayout() const { return oceanTextureDescLayout; }
        [[nodiscard]] vk::DescriptorSet getOceanTextureDescSet() const { return oceanTextureDescSet; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] const OceanFFTConfig& getConfig() const { return config; }

        // CPU-side displacement readback for physics
        void readbackDisplacementData();
        [[nodiscard]] float sampleHeightAt(const glm::vec2& worldXZ) const;

    private:
        void createTextures();
        void createSampler();
        void createDescriptorLayouts();
        void createPipelines();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void updateDescriptorSets();
        void transitionImagesInitial();

        void dispatchSpectrum(vk::CommandBuffer cmd);
        void dispatchTimeEvolve(vk::CommandBuffer cmd, float time);
        void dispatchFFT(vk::CommandBuffer cmd);
        void dispatchMerge(vk::CommandBuffer cmd);

        void insertComputeBarrier(vk::CommandBuffer cmd);

        void createReadbackBuffer();
        void destroyReadbackBuffer();
        void recordReadbackCopy(vk::CommandBuffer cmd);

        void destroyTextures();
        void destroyPipelines();
    };
}
