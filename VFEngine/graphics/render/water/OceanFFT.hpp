#pragma once

#include "OceanFFTTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
}

namespace render::water
{
    class OceanFFTResources;
    class OceanFFTPipelines;
    class OceanFFTReadback;

    class OceanFFT
    {
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

        [[nodiscard]] vk::DescriptorSetLayout getOceanTextureLayout() const;
        [[nodiscard]] vk::DescriptorSet getOceanTextureDescSet() const;
        [[nodiscard]] vk::ImageView getCausticView() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] const OceanFFTConfig& getConfig() const { return config; }
        [[nodiscard]] OceanFFTResources* getResources() const { return resources.get(); }

        // CPU-side displacement readback for physics
        void readbackDisplacementData();
        [[nodiscard]] float sampleHeightAt(const glm::vec2& worldXZ) const;
        // VK-1604: sample at an explicit patch UV (hex tiling supplies per-cell offset UVs).
        [[nodiscard]] float sampleHeightAtUV(const glm::vec2& uv) const;

    private:
        core::Device& device;
        OceanFFTConfig config;

        std::unique_ptr<OceanFFTResources> resources;
        std::unique_ptr<OceanFFTPipelines> pipelines;
        std::unique_ptr<OceanFFTReadback> readback;

        bool initialized = false;
        bool spectrumDirty = true;
        bool firstDispatch = true;

        // Persistent foam ping-pong: merge reads foam[historyIndex], writes foam[1 - historyIndex]
        uint32_t foamHistoryIndex = 0;
        float lastDispatchTime = -1.0f;
    };
}
