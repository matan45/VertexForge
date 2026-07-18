#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include "material/MaterialTypes.hpp"

namespace core
{
    class Device;
    class Shader;
}

namespace material
{
    struct MaterialData;
}

namespace render::mesh
{
    struct MaterialPipelineData
    {
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline opaquePipeline;
        vk::Pipeline maskedPipeline;
        vk::Pipeline translucentPipeline;
        vk::Pipeline additivePipeline;
        vk::Pipeline multiplyPipeline;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
        std::string shaderMapKey;
        bool valid = false;

        vk::Pipeline pipelineForBlendMode(material::BlendMode blendMode) const;
    };

    class MaterialShaderCache
    {
    private:
        std::string lastCompilationError;
        core::Device& device;
        vk::PipelineLayout pipelineLayout;
        vk::Extent2D swapchainExtent;
        vk::Format colorFormat = vk::Format::eUndefined;
        vk::Format depthFormat = vk::Format::eUndefined;
        bool initialized = false;
        std::unordered_map<std::string, MaterialPipelineData> cache;
        // Guards `cache` (find/insert/erase). Heavy pipeline building happens OUTSIDE the
        // lock; the mutex only covers the map operations. Required because warm-up jobs
        // (VK-1532) build pipelines on worker threads while the render thread also calls
        // getOrCreatePipeline. mutable so hasPipeline() can stay const.
        mutable std::mutex cacheMutex;
        // Set true by the render thread only while a frame is being recorded (VK-1532).
        // A pipeline MISS while this is set means warm-up failed to cover the permutation
        // and we are about to hitch the frame — logged (and asserted in Debug).
        std::atomic<bool> frameRecordingActive{false};

    public:
        explicit MaterialShaderCache(core::Device& device);
        ~MaterialShaderCache();

        void init(vk::PipelineLayout pipelineLayout,
                  vk::Extent2D swapchainExtent,
                  vk::Format colorFormat,
                  vk::Format depthFormat);
        
        const MaterialPipelineData* getOrCreatePipeline(const std::string& materialPath,
                                                        const material::MaterialData& materialData);

        // Build and cache the pipelines for a material off the render thread (VK-1532
        // warm-up). Thread-safe: the heavy compile/pipeline build runs without the lock,
        // and only the cache insert is synchronized. Returns true if the material is (now)
        // cached. Never touches lastCompilationError (that member is render-thread-owned).
        bool warmPipeline(const std::string& materialPath,
                          const material::MaterialData& materialData);

        // Toggle the "a frame is being recorded" flag used by the mid-frame guard.
        void setFrameRecording(bool recording)
        {
            frameRecordingActive.store(recording, std::memory_order_release);
        }

        void invalidate(const std::string& materialPath);

        void invalidateAll();

        void cleanUp();

        bool hasPipeline(const std::string& materialPath) const;

        const std::string& getLastCompilationError() const { return lastCompilationError; }

    private:
        // Compile shaders + create the 5 blend-mode pipelines into `outData`. Touches only
        // `outData` + session-fixed config (device/layout/extent/formats), so it is safe to
        // run concurrently from multiple threads. The compile error (if any) is returned via
        // `outError` rather than the shared member, so concurrent warm-up jobs don't race.
        bool compileAndCreatePipeline(const std::string& materialPath,
                                      const material::MaterialData& materialData,
                                      MaterialPipelineData& outData,
                                      std::string& outError);

        bool createPipelines(MaterialPipelineData& data);

        // Erase + destroy the cached entry for `materialPath`. Caller MUST hold cacheMutex.
        // Calls device waitIdle before destroying (may be bound by an in-flight frame), so
        // this is only used from the render-thread path, never from warm-up workers.
        void invalidateLocked(const std::string& materialPath);

        // Destroy the pipelines + shader of a freshly-built, never-bound MaterialPipelineData
        // (a discarded warm-up/render duplicate). No waitIdle — safe on any thread.
        void destroyPipelineData(MaterialPipelineData& data);

        // hasPipeline() body assuming cacheMutex is already held.
        bool hasPipelineLocked(const std::string& materialPath) const;

        static std::string hashShaderSource(const std::string& source);
    };
}
