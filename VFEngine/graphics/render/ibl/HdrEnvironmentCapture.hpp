#pragma once

// VK-1574 (Phase 2) — Non-blocking HDR IBL bake. Mirrors render::atmosphere::SkyEnvironmentCapture
// (the VK-1569 dynamic-sky capture) but sources an equirectangular HDR instead of the atmosphere
// LUTs, preserves today's IBL::init fidelity (1024^2 env cube, 512^2 x 10-mip prefilter roughness
// = m/9, 32^2 irradiance), and DOUBLE-BUFFERS the env cube too (it is the visible skybox), so the
// skybox + ambient swap atomically at publish() — no gray flash when a new HDR is applied.
//
// Work-item time-slicing is delegated to AmbientCaptureScheduler with prefilterMips = 10 => 72
// items (env(6) -> irradiance(6) -> prefilter(6 faces x 10 mips = 60)). The scheduler already
// generalizes over prefilterMips, so this needs no scheduler change and VK-1569 (default 5 mips /
// 42 items) stays byte-identical.
//
// Applying a new HDR NEVER blocks the render thread: the .vfHdr is decoded off-thread
// (ResourceManager::loadHDRAsync) and the source descriptor re-points + epoch flips only once the
// upload is resident (pollSource). The old live maps stay bound until the new cycle publishes.

#include "IBLTypes.hpp"
#include "atmosphere/AmbientCaptureScheduler.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace core
{
    class Device;
    class Shader;
    class Texture;
    class DeferredDeletionQueue;
}

namespace resource
{
    struct HDRData;
}

namespace render::ibl
{
    class BRDFLUTGenerator;

    class HdrEnvironmentCapture
    {
    public:
        explicit HdrEnvironmentCapture(core::Device& device);
        ~HdrEnvironmentCapture();

        HdrEnvironmentCapture(const HdrEnvironmentCapture&) = delete;
        HdrEnvironmentCapture& operator=(const HdrEnvironmentCapture&) = delete;

        // Allocate all persistent resources (double-buffered env/irradiance/prefilter, static BRDF
        // LUT, capture pipelines). Idempotent.
        void init(const vk::CommandPool& commandPool);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // The live maps the consumers bind (image views are stable for the object's life):
        //  - env cube    -> SkyboxRenderer (the visible skybox)
        //  - irradiance  -> mesh IBL descriptor b1
        //  - prefilter   -> mesh IBL descriptor b2
        //  - BRDF LUT    -> mesh IBL descriptor b3
        [[nodiscard]] const ImageData& getEnvLive() const { return envLive; }
        [[nodiscard]] const ImageData& getIrradianceLive() const { return irradianceLive; }
        [[nodiscard]] const ImageData& getPrefilterLive() const { return prefilterLive; }
        [[nodiscard]] const ImageData& getBrdfLUT() const { return brdfLUT; }

        // Request a new HDR environment. Non-blocking: kicks the off-thread decode and stores the
        // pending epoch (FNV-1a of the path). The source descriptor re-points + the epoch flips only
        // once the upload is resident (see pollSource); until then the old maps stay live.
        void setSource(const std::string& hdrPath);
        [[nodiscard]] bool hasSource() const { return hasSrc || pendingFuture.valid(); }

        // The epoch recordCapture should drive with (the resident source's path hash). Changes only
        // when a newly-decoded HDR becomes resident, which restarts the capture cycle.
        [[nodiscard]] uint64_t currentEpoch() const { return sourceEpoch; }

        // Per-frame, render thread, self-contained (no frame command buffer needed): if a pending
        // decode has completed AND the current cycle is idle (published), upload the HDR, re-point
        // the source descriptor, retire the old source, and flip sourceEpoch. Safe to call every
        // frame; a no-op when nothing is pending.
        void pollSource();

        // One-time blocking full-cycle capture + publish. Used only for the first bind / mode toggle
        // (scene load already blocks today). Ensures the pending source is resident first.
        void captureBlocking(uint64_t epoch);

        // Per-frame time-sliced capture recorded onto the frame-graph command buffer. Advances at
        // most `budget` work items; publishes staging->live once a full cycle completes; starts a
        // fresh cycle only when `epoch` differs after the previous cycle published. No GPU waits.
        void recordCapture(const vk::CommandBuffer& cmd, uint32_t budget, uint64_t epoch);

        // Optional: retire old source HDR textures without a waitIdle (else the Texture dtor stalls).
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

    private:
        void recordItem(const vk::CommandBuffer& cmd, const render::atmosphere::WorkItem& item);
        void recordEnvFace(const vk::CommandBuffer& cmd, uint32_t face);
        void recordIrradianceFace(const vk::CommandBuffer& cmd, uint32_t face);
        void recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip);
        void publish(const vk::CommandBuffer& cmd);

        void createImages(const vk::CommandBuffer& initCmd);
        void createDescriptors();
        void createPipelines();

        // Blocks on the pending decode (if any) and uploads it, re-pointing the source descriptor.
        void ensureSourceResident();
        // Uploads an already-decoded HDR, re-points srcDS, retires the old source, flips sourceEpoch.
        void promoteSource(const std::shared_ptr<resource::HDRData>& hdr);

        core::Device& device;
        bool initialized = false;
        vk::CommandPool commandPool{};

        core::DeferredDeletionQueue* deletionQueue = nullptr;

        // Equirectangular HDR source (bound to srcDS). Async-decoded, uploaded on the render thread.
        std::shared_ptr<core::Texture> hdrTexture;
        std::future<std::shared_ptr<resource::HDRData>> pendingFuture;
        uint64_t pendingEpoch = 0; // path hash of the source currently decoding (0 if none)
        uint64_t sourceEpoch = 0;  // path hash of the resident source the scheduler bakes
        bool hasSrc = false;
        // Old source textures kept alive until cleanup when no deletion queue is available.
        std::vector<std::shared_ptr<core::Texture>> retiredSources;

        std::shared_ptr<core::Shader> envShader;        // equirect HDR -> cube (push-const viewProj)
        std::shared_ptr<core::Shader> irradianceShader; // cube -> irradiance
        std::shared_ptr<core::Shader> prefilterShader;  // cube -> prefilter (push-const viewProj + roughness)

        vk::Buffer cubeVertexBuffer{};
        core::VulkanAllocation cubeVertexAllocation{};

        // Scratch + staging + live maps.
        ImageData envStaging{};        // 1024^2, 1 mip, sampled (written by env phase, read by irr/prefilter)
        ImageData stagingIrradiance{}; // 32^2, copy target then copied to live at publish
        ImageData stagingPrefilter{};  // 512^2, 10 mips
        ImageData envLive{};           // 1024^2, 1 mip, bound to the skybox (visible env)
        ImageData irradianceLive{};    // 32^2, bound to the mesh IBL descriptor
        ImageData prefilterLive{};     // 512^2, 10 mips, bound to the mesh IBL descriptor
        ImageData brdfLUT{};           // view-independent, generated once (static)

        OffScreenHelper helperHi{}; // 1024^2 color scratch (env + prefilter render target)
        OffScreenHelper helperLo{}; // 32^2 color scratch (irradiance render target)

        std::unique_ptr<BRDFLUTGenerator> brdfGenerator;

        vk::DescriptorPool descriptorPool{};
        vk::DescriptorSetLayout srcDSLayout{}; // b0 equirect HDR (sampler2D)
        vk::DescriptorSetLayout envDSLayout{}; // b0 env cubemap
        vk::DescriptorSet srcDS{};
        vk::DescriptorSet envDS{};

        vk::PipelineLayout envPipelineLayout{};
        vk::PipelineLayout irradiancePipelineLayout{};
        vk::PipelineLayout prefilterPipelineLayout{};
        vk::Pipeline envPipeline{};
        vk::Pipeline irradiancePipeline{};
        vk::Pipeline prefilterPipeline{};

        render::atmosphere::AmbientCaptureScheduler scheduler{};
        // Tracks the env staging cube's current layout so phase transitions are correct across frames.
        vk::ImageLayout envStagingLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        static constexpr uint32_t ENV_SIZE = 1024;       // visible skybox cube (matches EnvironmentCubemapGenerator)
        static constexpr uint32_t IRR_SIZE = 32;         // == ibl::IRRADIANCE_MAP_SIZE
        static constexpr uint32_t PREFILTER_SIZE = 512;  // == ibl::CUBE_MAP_SIZE
        static constexpr uint32_t PREFILTER_MIPS = 10;   // floor(log2(512))+1; roughness m/(mips-1) = m/9
        static constexpr vk::Format HDR_FORMAT = vk::Format::eR16G16B16A16Sfloat;
    };
}
