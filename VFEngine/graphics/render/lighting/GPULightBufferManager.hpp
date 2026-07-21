#pragma once

#include "GPULightTypes.hpp"
#include "../shadow/ShadowTypes.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <optional>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowSystem;
}

namespace render::lighting
{
    class GPULightBufferManager
    {
    private:
        core::Device& device;

        vk::Buffer directionalBuffer;
        core::VulkanAllocation directionalAllocation;

        vk::Buffer pointBuffer;
        core::VulkanAllocation pointAllocation;

        vk::Buffer spotBuffer;
        core::VulkanAllocation spotAllocation;

        // Per-frame staging buffers for CPU→GPU light data uploads
        struct LightStagingFrame
        {
            vk::Buffer directionalStagingBuffer;
            core::VulkanAllocation directionalStagingAllocation;
            void* directionalStagingMapped = nullptr;

            vk::Buffer pointStagingBuffer;
            core::VulkanAllocation pointStagingAllocation;
            void* pointStagingMapped = nullptr;

            vk::Buffer spotStagingBuffer;
            core::VulkanAllocation spotStagingAllocation;
            void* spotStagingMapped = nullptr;
        };

        std::array<LightStagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        vk::Buffer countsBuffer;
        core::VulkanAllocation countsAllocation;
        void* countsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        std::vector<GPUDirectionalLight> cpuDirectionalLights;
        std::vector<GPUPointLight> cpuPointLights;
        std::vector<GPUSpotLight> cpuSpotLights;

        uint32_t directionalCount = 0;
        uint32_t pointCount = 0;
        uint32_t spotCount = 0;

        std::vector<GPUDirectionalLight> prevDirectionalLights;
        std::vector<GPUPointLight> prevPointLights;
        std::vector<GPUSpotLight> prevSpotLights;
        uint32_t prevDirectionalCount = 0;
        uint32_t prevPointCount = 0;
        uint32_t prevSpotCount = 0;

        bool needsUpload = true;
        bool initialized = false;

        bool warnedDirectionalLimit = false;
        bool warnedPointLimit = false;
        bool warnedSpotLimit = false;

        shadow::ShadowSystem* shadowSystem = nullptr;
        float shadowIntensity = 0.5f;
        glm::vec3 sunColorMultiplier{1.0f}; // VK-1566: atmospheric tint for the index-0 directional light
        bool rtShadowActive = false;
        bool rtSpotShadowActive = false;
        bool rtPointShadowActive = false;
        std::unordered_set<uint32_t> registeredShadowLights;

        // Per-index entity id mirror of cpuSpotLights (for stable RT-slice assignment across frames).
        std::vector<uint32_t> cpuSpotLightEntityIds;
        // Which spot-light entity currently owns each RT mask slice, and whether that slot is live.
        std::array<uint32_t, LightConstants::MAX_RT_SPOT_LIGHTS> rtSpotSliceLights{};
        std::array<bool, LightConstants::MAX_RT_SPOT_LIGHTS> rtSpotSliceValid{};

        // Per-index entity id mirror of cpuPointLights (for stable RT-slice assignment across frames).
        std::vector<uint32_t> cpuPointLightEntityIds;
        // Which point-light entity currently owns each RT mask slice, and whether that slot is live.
        std::array<uint32_t, LightConstants::MAX_RT_POINT_LIGHTS> rtPointSliceLights{};
        std::array<bool, LightConstants::MAX_RT_POINT_LIGHTS> rtPointSliceValid{};

        struct PendingShadowReg
        {
            uint32_t entityId;
            shadow::ShadowMapType type;
            shadow::ShadowSettings settings;
        };
        std::vector<PendingShadowReg> pendingDirShadow;
        std::vector<PendingShadowReg> pendingPointShadow;
        std::vector<PendingShadowReg> pendingSpotShadow;

    public:
        explicit GPULightBufferManager(core::Device& device);
        ~GPULightBufferManager();

        GPULightBufferManager(const GPULightBufferManager&) = delete;
        GPULightBufferManager& operator=(const GPULightBufferManager&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateFromScene();
        void updateFromScene(const std::unordered_set<uint32_t>& visibleLightIds);
        void uploadToGPU(vk::CommandBuffer cmd);

        // Transient point lights appended after scene lights every updateFromScene
        // (e.g. VFX proxy lights from explosions/fires). Replaced wholesale each
        // frame; no entity, no shadow casting.
        struct TransientPointLight
        {
            glm::vec3 position{0.0f};
            glm::vec3 color{1.0f};
            float intensity = 1.0f;
            float radius = 10.0f;
        };
        void setTransientPointLights(std::vector<TransientPointLight> lights)
        {
            transientPointLights = std::move(lights);
        }
        void advanceStagingFrame() { currentStagingFrame = (currentStagingFrame + 1) % core::MAX_FRAMES_IN_FLIGHT; }

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        uint32_t getDirectionalLightCount() const { return directionalCount; }
        std::optional<glm::vec3> getFirstDirectionalLightDirection() const
        {
            if (directionalCount > 0)
                return cpuDirectionalLights[0].direction;
            return std::nullopt;
        }
        uint32_t getPointLightCount() const { return pointCount; }
        uint32_t getSpotLightCount() const { return spotCount; }

        vk::Buffer getPointBuffer() const { return pointBuffer; }
        vk::Buffer getSpotBuffer() const { return spotBuffer; }

        void setShadowSystem(shadow::ShadowSystem* system) { shadowSystem = system; }
        void setShadowIntensity(float intensity);
        float getShadowIntensity() const { return shadowIntensity; }

        // VK-1566: multiplier applied to the FIRST directional light's color (index 0 = the
        // sun, by engine convention). Fed each frame with the atmospheric sun-transmittance
        // tint; vec3(1) leaves the light unchanged. Applied in collectDirectionalLights, so
        // detectChanges() picks up the change and re-uploads for free.
        void setSunColorMultiplier(const glm::vec3& multiplier) { sunColorMultiplier = multiplier; }
        void setRTShadowActive(bool active);
        void setRTSpotShadowActive(bool active);
        void setRTPointShadowActive(bool active);

        // Runtime flag: true when RT directional shadows produced a mask last frame and the
        // fragment shaders are overriding the directional VSM clipmap full-screen. Used to skip
        // rendering the (then-unsampled) clipmap pages — see ShadowSystem::setDirectionalRTOverrideActive.
        [[nodiscard]] bool getRTShadowActive() const { return rtShadowActive; }

        // Selects the closest/brightest shadow-casting spot lights (up to budget, capped at
        // MAX_RT_SPOT_LIGHTS), assigns each an RT mask slice (writes rtMaskSlice into the GPU
        // spot buffer), and returns the slices whose owning light changed this frame (their
        // denoiser history must be reset). Lights outside the budget keep rtMaskSlice = -1 (VSM).
        std::vector<uint32_t> assignRTSpotSlices(const glm::vec3& cameraPos, uint32_t budget);
        // Spot lights currently promoted to an RT slice (rtMaskSlice >= 0), in slice order.
        const std::array<uint32_t, LightConstants::MAX_RT_SPOT_LIGHTS>& getRTSpotSliceLights() const { return rtSpotSliceLights; }
        const std::array<bool, LightConstants::MAX_RT_SPOT_LIGHTS>& getRTSpotSliceValid() const { return rtSpotSliceValid; }
        // Returns the GPU spot-light record for an index < getSpotLightCount() (RT dispatch needs
        // position/direction/cone to drive the per-light ray + cone cull).
        const GPUSpotLight& getSpotLight(uint32_t index) const { return cpuSpotLights[index]; }
        // Maps a spot entity id to its current cpuSpotLights index this frame, or -1 if absent.
        int getSpotIndexForEntity(uint32_t entityId) const
        {
            for (uint32_t i = 0; i < spotCount; ++i)
                if (cpuSpotLightEntityIds[i] == entityId) return static_cast<int>(i);
            return -1;
        }

        // Selects the closest/brightest shadow-casting point lights (up to budget, capped at
        // MAX_RT_POINT_LIGHTS), assigns each an RT mask slice (writes rtMaskSlice into the GPU
        // point buffer), and returns the slices whose owning light changed this frame (their
        // denoiser history must be reset). Lights outside the budget keep rtMaskSlice = -1 (VSM).
        std::vector<uint32_t> assignRTPointSlices(const glm::vec3& cameraPos, uint32_t budget);
        // Point lights currently promoted to an RT slice (rtMaskSlice >= 0), in slice order.
        const std::array<uint32_t, LightConstants::MAX_RT_POINT_LIGHTS>& getRTPointSliceLights() const { return rtPointSliceLights; }
        const std::array<bool, LightConstants::MAX_RT_POINT_LIGHTS>& getRTPointSliceValid() const { return rtPointSliceValid; }
        // Returns the GPU point-light record for an index < getPointLightCount() (RT dispatch needs
        // position/radius to drive the per-light ray + radius cull).
        const GPUPointLight& getPointLight(uint32_t index) const { return cpuPointLights[index]; }
        // Maps a point entity id to its current cpuPointLights index this frame, or -1 if absent.
        int getPointIndexForEntity(uint32_t entityId) const
        {
            for (uint32_t i = 0; i < pointCount && i < cpuPointLightEntityIds.size(); ++i)
                if (cpuPointLightEntityIds[i] == entityId) return static_cast<int>(i);
            return -1;
        }

    private:
        void createBuffers();
        void destroyBuffers();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptors();

        void collectDirectionalLights(const std::unordered_set<uint32_t>* visibleLightIds,
                                       std::vector<PendingShadowReg>& pendingShadow);
        void collectPointLights(const std::unordered_set<uint32_t>* visibleLightIds,
                                std::vector<PendingShadowReg>& pendingShadow);
        void appendTransientPointLights();
        void collectSpotLights(const std::unordered_set<uint32_t>* visibleLightIds,
                               std::vector<PendingShadowReg>& pendingShadow);
        void processPendingShadowRegistrations();
        void updateShadowRegistration(uint32_t entityId, shadow::ShadowMapType type,
                                       const shadow::ShadowSettings& settings);
        void cleanupStaleShadowRegistrations();
    public:
        void preWarmShadowsForLights(const std::vector<uint32_t>& entityIds);
    private:
        void updateCountsBuffer();
        bool detectChanges();

        std::vector<TransientPointLight> transientPointLights;
    };
}
