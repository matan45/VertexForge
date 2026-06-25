#pragma once

// VK-1433 Phase 1 — Prefab Rig Preview skeleton/socket/IK overlay.
//
// Offscreen overlay-pass wrapper, modeled on PreviewGridRenderer: it owns a line-list
// graphics pipeline (DebugLineVertex layout, a single viewProj push constant) plus a
// per-image dynamic vertex buffer, and draws an ImmediateDebugDrawList of debug lines
// (skeleton bones, socket axis triads, IK targets) into the preview's offscreen color+
// depth target. The draw is built CPU-side by the controller each frame.
//
// Like the grid it self-manages its layout transitions (color SHADER_READ_ONLY ->
// COLOR_ATTACHMENT around the pass, ending in SHADER_READ_ONLY) and sets the dynamic
// rasterization sample count to the offscreen target's. Unlike the grid it draws with
// DEPTH TEST OFF so bones/markers read THROUGH the mesh — the desired default for a rig
// validation overlay. It reuses resources/shaders/tools/immediate_debug.glsl.

#include "../../core/OffScreen.hpp"
#include "../tools/ImmediateDebugTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::preview
{
    class PreviewSkeletonOverlayRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::shared_ptr<core::Shader> shader;

        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        // Per swapchain image so the host upload for image N never races a draw still
        // reading image N-1's buffer (MAX_FRAMES_IN_FLIGHT != image count; index by imageIndex).
        std::vector<vk::Buffer> vertexBuffers;
        std::vector<core::VulkanAllocation> vertexAllocations;
        std::vector<uint32_t> vertexCounts;
        std::vector<vk::DeviceSize> bufferSizes;

        bool initialized = false;

    public:
        explicit PreviewSkeletonOverlayRenderer(core::Device& device, core::SwapChain& swapChain,
                                                core::OffscreenResources& offscreenResources);
        ~PreviewSkeletonOverlayRenderer();

        PreviewSkeletonOverlayRenderer(const PreviewSkeletonOverlayRenderer&) = delete;
        PreviewSkeletonOverlayRenderer& operator=(const PreviewSkeletonOverlayRenderer&) = delete;

        void init();
        void cleanUp();
        void cleanUpShader();

        // Uploads `lines` to this image's vertex buffer, then draws them over the offscreen
        // color+depth target (loading existing contents). No-op if `lines` is empty.
        void render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                    const glm::mat4& view, const glm::mat4& projection,
                    const render::mesh::ImmediateDebugDrawList& lines);

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline();
        void uploadLines(uint32_t imageIndex, const render::mesh::ImmediateDebugDrawList& lines);
        void destroyBuffer(size_t imageIndex);
    };
}
