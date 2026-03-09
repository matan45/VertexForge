#pragma once

#include "GITypes.hpp"
#include "ProbeStorageBuffer.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
}

namespace render::gi
{
    class RadianceCascadeManager
    {
    private:
        core::Device& device;

        RadianceCascadeConfig config;
        GISettings settings;
        std::vector<CascadeLevel> cascades;

        std::unique_ptr<ProbeStorageBuffer> probeStorage;

        glm::vec3 lastCameraPosition{0.0f};
        bool initialized = false;
        uint32_t frameIndex = 0;
        uint32_t totalProbeCount = 0;

    public:
        explicit RadianceCascadeManager(core::Device& device);
        ~RadianceCascadeManager();

        RadianceCascadeManager(const RadianceCascadeManager&) = delete;
        RadianceCascadeManager& operator=(const RadianceCascadeManager&) = delete;

        void init(const GISettings& giSettings);
        void cleanup();

        void updateCameraPosition(const glm::vec3& cameraPos);
        void beginFrame();

        // Returns indices of probes to update this frame for each cascade
        struct ProbeUpdateBatch
        {
            uint32_t cascadeIndex;
            uint32_t probeStartOffset; // Global offset
            uint32_t probeCount;       // Number to update
        };
        std::vector<ProbeUpdateBatch> getProbeUpdateBatches() const;

        void applySettings(const GISettings& newSettings);
        const GISettings& getSettings() const { return settings; }

        // Accessors
        uint32_t getCascadeCount() const { return static_cast<uint32_t>(cascades.size()); }
        const CascadeLevel& getCascade(uint32_t index) const { return cascades[index]; }
        uint32_t getTotalProbeCount() const { return totalProbeCount; }
        bool isInitialized() const { return initialized; }
        uint32_t getFrameIndex() const { return frameIndex; }

        ProbeStorageBuffer* getProbeStorage() const { return probeStorage.get(); }

        // GPU descriptor access
        vk::DescriptorSetLayout getProbeDataLayout() const;
        vk::DescriptorSet getProbeDataDescSet() const;
        vk::DescriptorSetLayout getCascadeInfoLayout() const;
        vk::DescriptorSet getCascadeInfoDescSet() const;

        GIDebugStats getDebugStats() const;

    private:
        void buildCascades();
        void scrollCascadeGrid(uint32_t cascadeIndex, const glm::vec3& newOrigin);
    };
}
