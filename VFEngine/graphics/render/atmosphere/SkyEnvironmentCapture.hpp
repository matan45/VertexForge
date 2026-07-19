#pragma once

// VK-1569 — Dynamic sky -> IBL ambient: time-sliced GPU capture of the atmosphere sky into
// persistent IBL cubemaps (irradiance + prefiltered specular) whose image views NEVER change,
// so the mesh pipeline's IBL descriptor set is written exactly once (at the mode toggle) and the
// per-frame capture just renders + copies into the same images. See AmbientCaptureScheduler for
// the work-item time-slicing (env(6) -> irradiance(6) -> prefilter(30) = 42 items, published once
// per completed cycle).

#include "../ibl/IBLTypes.hpp"
#include "atmosphere/AmbientCaptureScheduler.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::ibl
{
    class BRDFLUTGenerator;
}

namespace render::atmosphere
{
    class AtmospherePipeline;

    class SkyEnvironmentCapture
    {
    public:
        explicit SkyEnvironmentCapture(core::Device& device);
        ~SkyEnvironmentCapture();

        SkyEnvironmentCapture(const SkyEnvironmentCapture&) = delete;
        SkyEnvironmentCapture& operator=(const SkyEnvironmentCapture&) = delete;

        // Allocate all persistent resources and capture the atmosphere's stable LUT/param handles.
        // Must be called while the atmosphere pipeline is initialized.
        void init(const vk::CommandPool& commandPool, AtmospherePipeline& atmosphere);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // The live IBL maps the mesh pipeline binds (image views are stable for the object's life).
        [[nodiscard]] const ibl::ImageData& getIrradianceLive() const { return irradianceLive; }
        [[nodiscard]] const ibl::ImageData& getPrefilterLive() const { return prefilterLive; }
        [[nodiscard]] const ibl::ImageData& getBrdfLUT() const { return brdfLUT; }

        // Blocking full-cycle capture + publish (used once on the OFF->ON toggle so the first scene
        // frame samples valid ambient instead of black).
        void captureBlocking(uint64_t epoch);

        // Per-frame time-sliced capture recorded onto the frame-graph command buffer. Advances at
        // most `budget` work items; publishes staging->live once a full cycle completes; starts a
        // fresh cycle only when `epoch` changes after the previous cycle published (a static sky is
        // captured once, a changing sky rolls with ~1-cycle latency and never stalls).
        void recordCapture(const vk::CommandBuffer& cmd, uint32_t budget, uint64_t epoch);

    private:
        // Per-work-item recording (all onto the given command buffer; no per-face fences).
        void recordItem(const vk::CommandBuffer& cmd, const WorkItem& item);
        void recordEnvFace(const vk::CommandBuffer& cmd, uint32_t face);
        void recordIrradianceFace(const vk::CommandBuffer& cmd, uint32_t face);
        void recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip);
        void publish(const vk::CommandBuffer& cmd);

        void createImages(const vk::CommandBuffer& initCmd);
        void createDescriptors();
        void createPipelines();

        core::Device& device;
        bool initialized = false;

        // Stable atmosphere handles captured at init (created in AtmospherePipeline::init, untouched
        // by recreate()).
        vk::ImageView skyViewView{};
        vk::ImageView transmittanceView{};
        vk::Buffer atmosphereParamsBuffer{};
        vk::Sampler lutSampler{};

        vk::CommandPool commandPool{};

        std::shared_ptr<core::Shader> skyShader;
        std::shared_ptr<core::Shader> irradianceShader;
        std::shared_ptr<core::Shader> prefilterShader;

        // Cube geometry shared by all three capture pipelines.
        vk::Buffer cubeVertexBuffer{};
        core::VulkanAllocation cubeVertexAllocation{};

        // Scratch + staging + live maps.
        ibl::ImageData envCube{};          // 128^2, 1 mip, sampled by irradiance/prefilter
        ibl::ImageData stagingIrradiance{}; // 32^2, copy target then copied to live at publish
        ibl::ImageData stagingPrefilter{};  // 128^2, 5 mips
        ibl::ImageData irradianceLive{};    // 32^2, bound to the mesh IBL descriptor
        ibl::ImageData prefilterLive{};     // 128^2, 5 mips, bound to the mesh IBL descriptor
        ibl::ImageData brdfLUT{};           // view-independent, generated once (kept static)

        ibl::OffScreenHelper helperHi{}; // 128^2 color scratch (env + prefilter render target)
        ibl::OffScreenHelper helperLo{}; // 32^2 color scratch (irradiance render target)

        std::unique_ptr<ibl::BRDFLUTGenerator> brdfGenerator;

        vk::DescriptorPool descriptorPool{};
        vk::DescriptorSetLayout skyDSLayout{};  // b0 skyView, b1 transmittance, b2 params
        vk::DescriptorSetLayout envDSLayout{};  // b0 env cubemap
        vk::DescriptorSet skyDS{};
        vk::DescriptorSet envDS{};

        vk::PipelineLayout skyPipelineLayout{};
        vk::PipelineLayout irradiancePipelineLayout{};
        vk::PipelineLayout prefilterPipelineLayout{};
        vk::Pipeline skyPipeline{};
        vk::Pipeline irradiancePipeline{};
        vk::Pipeline prefilterPipeline{};

        AmbientCaptureScheduler scheduler{};
        // Tracks the env cube's current layout so phase transitions are correct across frames/restarts.
        vk::ImageLayout envLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        static constexpr uint32_t ENV_SIZE = 128;
        static constexpr uint32_t IRR_SIZE = 32; // == ibl::IRRADIANCE_MAP_SIZE
        static constexpr uint32_t PREFILTER_SIZE = 128;
        static constexpr uint32_t PREFILTER_MIPS = 5; // roughness m/(mips-1) = m/4; consumer LOD 0..4
        static constexpr vk::Format HDR_FORMAT = vk::Format::eR16G16B16A16Sfloat;
    };
}
