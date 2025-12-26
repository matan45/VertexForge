#pragma once

#include "GPUDrivenTypes.hpp"
#include "MergedMeshBuffer.hpp"
#include "IndirectDrawBuffer.hpp"
#include "BindlessTextureManager.hpp"
#include "GPUCullLODPipeline.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <unordered_set>

namespace core {
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::mesh {
    class MeshGPUCache;
    class MaterialTextureCache;
    struct MeshRenderData;
}

namespace render::gpudriven {

    /**
     * GPU-Driven Renderer Facade
     *
     * Orchestrates the entire GPU-driven rendering pipeline:
     * 1. Merged mesh buffers (all geometry in single buffers)
     * 2. GPU culling + LOD selection via compute shader
     * 3. Indirect drawing with bindless textures
     *
     * This class manages the transition from 400+ CPU draw calls to 1 indirect draw call.
     */
    class GPUDrivenRenderer {
    public:
        GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenRenderer();

        // Non-copyable
        GPUDrivenRenderer(const GPUDrivenRenderer&) = delete;
        GPUDrivenRenderer& operator=(const GPUDrivenRenderer&) = delete;

        /**
         * Initialize all GPU-driven rendering resources.
         * @param iblDescriptorSetLayout The IBL descriptor set layout (for camera + IBL textures)
         * @param renderPass The render pass to use for the graphics pipeline
         */
        void init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass);

        /**
         * Cleanup all GPU resources.
         */
        void cleanup();

        /**
         * Rebuild merged mesh buffers from the mesh cache.
         * Call this when meshes are loaded/unloaded.
         */
        void rebuildMergedBuffer(const mesh::MeshGPUCache& cache);

        /**
         * Register a texture for bindless access.
         * @return Bindless texture index
         */
        uint32_t registerTexture(const std::string& path, vk::ImageView view, vk::Sampler sampler);

        /**
         * Set the default fallback texture (1x1 white).
         */
        void setDefaultTexture(vk::ImageView view, vk::Sampler sampler);

        /**
         * Update scene data for rendering.
         * Call this each frame with the list of opaque objects to render.
         *
         * @param opaqueObjects List of mesh render data (opaque only, translucent handled separately)
         * @param cache Mesh GPU cache for looking up mesh data
         * @param view Camera view matrix
         * @param projection Camera projection matrix
         * @param cameraPosition Camera world position
         * @param nearPlane Camera near plane distance
         * @param farPlane Camera far plane distance
         */
        void updateScene(
            const std::vector<mesh::MeshRenderData>& opaqueObjects,
            const mesh::MeshGPUCache& cache,
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane
        );

        /**
         * Dispatch GPU culling compute shader.
         * Must be called BEFORE beginRenderPass (compute dispatch cannot be inside render pass).
         *
         * @param cmd Command buffer to record into
         */
        void dispatchCompute(vk::CommandBuffer cmd);

        /**
         * Record graphics draw commands.
         * Must be called AFTER beginRenderPass (inside render pass).
         *
         * @param cmd Command buffer to record into
         * @param iblDescriptorSet IBL descriptor set (camera UBO + IBL textures)
         */
        void renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet);

        /**
         * Enable/disable GPU-driven rendering.
         */
        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

        /**
         * Enable/disable frustum culling.
         */
        void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled = enabled; }
        bool isFrustumCullingEnabled() const { return frustumCullingEnabled; }

        /**
         * Enable/disable LOD selection.
         */
        void setLODSelectionEnabled(bool enabled) { lodSelectionEnabled = enabled; }
        bool isLODSelectionEnabled() const { return lodSelectionEnabled; }

        /**
         * Get rendering statistics.
         */
        const GPUDrivenStats& getStats() const { return stats; }

        /**
         * Check if renderer is initialized.
         */
        bool isInitialized() const { return initialized; }

        /**
         * Get the bindless texture descriptor set layout.
         */
        vk::DescriptorSetLayout getBindlessTextureLayout() const;

        /**
         * Set the material texture cache for texture loading.
         * This cache is used to load and access textures for bindless rendering.
         */
        void setMaterialTextureCache(mesh::MaterialTextureCache* cache) { materialTextureCache = cache; }

        /**
         * Register textures from a material with the bindless texture manager.
         * @param materialPath Path to the material
         * @return True if any textures were registered
         */
        bool registerMaterialTextures(const std::string& materialPath);

    private:
        core::Device& device;
        core::SwapChain& swapChain;

        // Sub-components
        std::unique_ptr<MergedMeshBuffer> mergedBuffer;
        std::unique_ptr<IndirectDrawBuffer> indirectBuffer;
        std::unique_ptr<BindlessTextureManager> bindlessTextures;
        std::unique_ptr<GPUCullLODPipeline> cullPipeline;

        // Graphics pipeline resources
        std::unique_ptr<core::Shader> meshShader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout graphicsPipelineLayout;
        vk::DescriptorSetLayout perDrawDataLayout;
        vk::DescriptorPool perDrawDataPool;
        vk::DescriptorSet perDrawDataDescriptorSet;

        // Camera UBO for compute shader
        vk::Buffer cameraBuffer;
        vk::DeviceMemory cameraBufferMemory;
        void* cameraMapped = nullptr;
        GPUCameraData cameraData{};

        // State
        bool initialized = false;
        bool enabled = false;
        bool frustumCullingEnabled = true;
        bool lodSelectionEnabled = true;
        uint32_t frameIndex = 0;

        // Statistics
        GPUDrivenStats stats{};

        // Cached for updateScene
        vk::DescriptorSetLayout cachedIBLLayout;
        vk::RenderPass cachedRenderPass;

        // Material texture cache (optional, for texture loading)
        mesh::MaterialTextureCache* materialTextureCache = nullptr;

        // Registered material paths (to avoid re-registering textures each frame)
        std::unordered_set<std::string> registeredMaterialPaths;

        // Helper methods
        void createCameraBuffer();
        void createGraphicsPipeline(vk::DescriptorSetLayout iblLayout, vk::RenderPass renderPass);
        void createPerDrawDataDescriptor();
        void updateCameraData(
            const glm::mat4& view,
            const glm::mat4& projection,
            const glm::vec3& cameraPosition,
            float nearPlane,
            float farPlane
        );
        void extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6]);
    };

}
