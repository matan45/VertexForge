#pragma once

// VK-1577 — bakes ReflectionProbeComponents into prefiltered specular cubemaps.
//
// Structurally a mirror of render::ibl::HdrEnvironmentCapture (VK-1574) with two differences:
//
//  1. The env source is a REAL SCENE RENDER, not a shader pass. Faces come from
//     RenderTextureViewPort::render() — the same arbitrary-camera renderer the minimap uses — so
//     probes see actual geometry. That half therefore runs at the render-texture hook point (its own
//     submit), while the prefilter half is recorded onto the frame-graph command buffer like every
//     other capture in the engine.
//
//  2. There is no irradiance phase. Probes are specular-only: diffuse ambient stays on the global
//     IBL + DDGI. AmbientCaptureScheduler is reused unchanged with irrFaces = 0, giving
//     env(6) + prefilter(6 faces x 5 mips) = 36 items per probe.
//
// Only ONE env scratch cube and ONE staging prefilter exist; probes bake strictly one at a time and
// publish into their own persistent live cube. Live image VIEWS never change, so the set-0 probe
// descriptors are written when a slot is allocated, never per frame.

#include "ReflectionProbeTypes.hpp"
#include "ReflectionProbeBufferManager.hpp"
#include "../ibl/IBLTypes.hpp"
#include "atmosphere/AmbientCaptureScheduler.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render
{
    class RenderTextureViewPort;
    class RenderPassHandler;
}

namespace render::probe
{
    class ReflectionProbeManager
    {
    public:
        ReflectionProbeManager(core::Device& device, core::SwapChain& swapChain);
        ~ReflectionProbeManager();

        ReflectionProbeManager(const ReflectionProbeManager&) = delete;
        ReflectionProbeManager& operator=(const ReflectionProbeManager&) = delete;

        void init(const vk::CommandPool& commandPool);
        void cleanup();
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // The persistent per-slot prefilter cubes bound at set 0 binding 4. Views are stable, so a
        // slot's descriptor is written once. Unallocated slots carry a null imageView and the
        // pipeline substitutes the global prefilter map.
        [[nodiscard]] const std::array<ibl::ImageData, MAX_REFLECTION_PROBES>& getProbeCubes() const
        {
            return probeCubes;
        }

        [[nodiscard]] ReflectionProbeBufferManager& getBufferManager() { return bufferManager; }
        [[nodiscard]] const ReflectionProbeBufferManager& getBufferManager() const { return bufferManager; }

        // True once at least one probe has published a capture — this is what drives the
        // REFLECTION_PROBES_ENABLED pipeline permutation. Flipping it recreates pipelines, so it is
        // deliberately edge-triggered on "any slot ready", not on a per-frame probe count.
        [[nodiscard]] bool hasAnyBakedProbe() const { return anySlotReady; }

        // ---- frame hooks -------------------------------------------------------------------
        // (a) Called at the render-texture point, BEFORE the frame graph. Renders at most one cube
        //     face of the probe currently baking; each face is its own submit, so a full 8-probe
        //     scene converges in ~48 frames without ever hitching.
        void tickSceneCapture(RenderPassHandler* passHandler);
        // (b) Recorded onto the frame-graph command buffer. Runs the GGX prefilter work items for
        //     the probe whose env faces are complete, and publishes staging -> that probe's live cube.
        void recordGraphWork(const vk::CommandBuffer& cmd, uint32_t budget);

    private:
        void createImages(const vk::CommandBuffer& initCmd);
        void createDescriptors();
        void createPipelines();
        void ensureCaptureViewport();

        // Renders one scene face into the capture viewport and copies it into the env scratch cube.
        void captureSceneFace(RenderPassHandler* passHandler, const ResolvedProbe& probe, uint32_t face);
        void recordPrefilterFace(const vk::CommandBuffer& cmd, uint32_t face, uint32_t mip);
        void publishToSlot(const vk::CommandBuffer& cmd, uint32_t slot);

        // Picks the next probe needing a bake; returns false when everything is up to date.
        bool selectNextDirtyProbe();

        core::Device& device;
        core::SwapChain& swapChain;
        bool initialized = false;
        vk::CommandPool commandPool{};

        ReflectionProbeBufferManager bufferManager;

        // Dedicated 128x128 HDR scene renderer for face capture (tonemap OFF — probes store raw HDR).
        std::unique_ptr<RenderTextureViewPort> captureViewport;

        std::shared_ptr<core::Shader> prefilterShader;

        vk::Buffer cubeVertexBuffer{};
        core::VulkanAllocation cubeVertexAllocation{};

        ibl::ImageData envScratch{};       // 128^2, 1 mip, sampled — one probe's 6 scene faces
        ibl::ImageData stagingPrefilter{}; // 128^2, 5 mips — GGX output before publish
        std::array<ibl::ImageData, MAX_REFLECTION_PROBES> probeCubes{}; // persistent, bound to set 0 b4
        ibl::OffScreenHelper helperColor{}; // 128^2 color scratch (prefilter render target)

        vk::DescriptorPool descriptorPool{};
        vk::DescriptorSetLayout envDSLayout{}; // b0 env scratch cubemap
        vk::DescriptorSet envDS{};

        vk::PipelineLayout prefilterPipelineLayout{};
        vk::Pipeline prefilterPipeline{};

        render::atmosphere::AmbientCaptureScheduler scheduler{};
        vk::ImageLayout envScratchLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Which slot is mid-bake, and whether its 6 scene faces are done.
        int32_t activeSlot = -1;
        entt::entity activeEntity{entt::null};
        bool anySlotReady = false;

        static constexpr uint32_t CUBE_SIZE = PROBE_CUBE_SIZE;  // 128
        static constexpr uint32_t PREFILTER_MIPS = PROBE_CUBE_MIPS; // 5 => roughness m/4
        static constexpr vk::Format HDR_FORMAT = vk::Format::eR16G16B16A16Sfloat;
    };
}
