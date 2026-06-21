#pragma once

#include "PostProcessEffect.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../../services/data/PostProcessEffectTypes.hpp"
#include <glm/glm.hpp>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class DeferredDeletionQueue;
    struct OffscreenResources;
}

namespace render::postprocess
{
    class PluginPostProcessEffect;

    struct SunInfo
    {
        glm::vec2 screenPos{0.5f};
        bool hasSun = false;
    };

    struct CameraInfo
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        glm::vec3 cameraPosition{0.0f};
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 prevViewMatrix{1.0f};
        glm::mat4 prevProjectionMatrix{1.0f};
        glm::mat4 unjitteredProjectionMatrix{1.0f};
        glm::vec2 jitterOffset{0.0f};
        uint32_t frameIndex = 0;
        float time = 0.0f;
        bool isUnderwater = false;
        float submersionFactor = 0.0f;
        float waterHeight = 0.0f;
    };

    struct PingPongTarget
    {
        vk::Image image;
        core::VulkanAllocation allocation;
        vk::ImageView imageView;
        vk::ImageLayout currentLayout = vk::ImageLayout::eUndefined;
    };

    class PostProcessPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        vk::Format sceneColorFormat = vk::Format::eUndefined;
        vk::Sampler linearSampler;
        vk::DescriptorSetLayout inputDescriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        PingPongTarget targetA{};
        PingPongTarget targetB{};

        // Display-resolution ping-pong targets (only used for post-upscale effects)
        PingPongTarget displayTargetA{};
        PingPongTarget displayTargetB{};

        vk::DescriptorSet descriptorSetA;
        vk::DescriptorSet descriptorSetB;
        vk::DescriptorSet displayDescriptorSetA;
        vk::DescriptorSet displayDescriptorSetB;
        std::vector<vk::DescriptorSet> sceneDescriptorSets;

        SunInfo sunInfo{};
        CameraInfo cameraInfo{};

        std::vector<std::unique_ptr<PostProcessEffect>> effects;
        std::optional<float> autoExposureOverride;
        // VK-1419: snapshot of the last-applied tone-mapping sub-settings (see getToneMappingSettings).
        ::postprocess::ToneMappingSettings lastToneMapping{};
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        // --- Plugin effect registry (VK-1409) ---
        // A move-only deferred op; applied on the render thread in drainPendingOps().
        struct PendingOp
        {
            enum class Kind { Add, Remove, SetEnabled, SetParams } kind;
            uint64_t id = 0;
            std::unique_ptr<PluginPostProcessEffect> effect; // Add only
            bool enabled = false;                            // SetEnabled only
            std::vector<std::byte> params;                   // SetParams only
        };
        std::mutex pendingMutex;
        std::vector<PendingOp> pendingOps;
        std::atomic<uint64_t> nextPluginEffectId{1};
        // Render-thread-only: maps handle id -> non-owning ptr (owner is `effects`).
        std::unordered_map<uint64_t, PluginPostProcessEffect*> pluginEffects;

    public:
        explicit PostProcessPipeline(core::Device& device, core::SwapChain& swapChain,
                                     core::OffscreenResources& offscreenResources);
        ~PostProcessPipeline();

        void execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        /// Execute only pre-upscale effects (at render resolution).
        void executePreUpscale(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        /// Execute only post-upscale effects (at display resolution).
        /// sourceImage/sourceView: the upscaled image to use as initial input.
        void executePostUpscale(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                vk::Image sourceImage, vk::ImageView sourceView);

        void recreate();
        void cleanup();

        void addEffect(std::unique_ptr<PostProcessEffect> effect);
        void removeEffect(::postprocess::EffectType type);
        void updateSettings(const ::postprocess::PostProcessSettings& settings);
        void applySettings(const ::postprocess::PostProcessSettings& settings);

        // Plugin-registered full-screen effects (VK-1409). These are handle-keyed
        // (plugin effects all share EffectType::PluginCustom) and thread-safe: the
        // calls below may run on the main thread while execute() runs on the render
        // thread, so they only enqueue ops, drained at the top of each execute*().
        plugin::PostProcessEffectHandle addPluginEffect(std::string fragmentGlsl,
                                                        uint32_t priority,
                                                        uint32_t paramsSize,
                                                        bool startEnabled,
                                                        std::string debugName);
        void removePluginEffect(plugin::PostProcessEffectHandle handle);
        void setPluginEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled);
        void setPluginEffectParams(plugin::PostProcessEffectHandle handle,
                                   std::vector<std::byte> params);

        void setDeletionQueue(core::DeferredDeletionQueue* queue);
        void setSunData(const glm::vec2& screenPos, bool hasSun);
        const SunInfo& getSunData() const { return sunInfo; }

        void setCameraData(const CameraInfo& incoming);
        const CameraInfo& getCameraData() const { return cameraInfo; }

        bool hasEnabledEffects() const;
        bool isInitialized() const { return initialized; }
        std::optional<float> getComputedExposure() const { return autoExposureOverride; }

        // VK-1419: the most recently applied tone-mapping sub-settings. Render textures build
        // their own ToneMappingEffect and feed it these so their look matches the main viewport.
        const ::postprocess::ToneMappingSettings& getToneMappingSettings() const { return lastToneMapping; }

        vk::Format getColorFormat() const { return sceneColorFormat; }
        vk::DescriptorSetLayout getInputDescriptorSetLayout() const { return inputDescriptorSetLayout; }
        vk::Sampler getLinearSampler() const { return linearSampler; }

    private:
        void lazyInit();
        void createSampler();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createPingPongTargets();
        void createDescriptorSets();
        void updateDescriptorSet(vk::DescriptorSet set, vk::ImageView imageView);

        void cleanupPingPongTargets();
        void sortEffects();

        // Apply queued plugin-effect ops on the render thread. Called first thing
        // in each execute*() so a freshly-registered effect is live this frame.
        void drainPendingOps();
    };
}
