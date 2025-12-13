#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <material/MaterialTypes.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace core {
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::material {

    // UBO for material parameters (passed via descriptor set)
    struct MaterialParameterUBO {
        alignas(16) glm::vec4 albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
        alignas(4) float metallic = 0.0f;
        alignas(4) float roughness = 0.5f;
        alignas(4) float ao = 1.0f;
        alignas(4) float emission = 0.0f;
        // Additional custom parameters can be added here
        alignas(16) glm::vec4 customVec4_0{ 0.0f };
        alignas(16) glm::vec4 customVec4_1{ 0.0f };
        alignas(16) glm::vec4 customVec4_2{ 0.0f };
        alignas(16) glm::vec4 customVec4_3{ 0.0f };
    };

    // Cached pipeline data for a compiled material
    struct CompiledMaterialPipeline {
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;
        size_t shaderHash = 0;  // Hash of vertex + fragment shader code
        bool valid = false;
    };

    // Per-material instance data (parameters + descriptor set)
    struct MaterialInstance {
        std::string materialPath;
        vk::Buffer parameterUBO;
        vk::DeviceMemory parameterUBOMemory;
        vk::DescriptorSet descriptorSet;
        size_t pipelineHash = 0;  // Links to which compiled pipeline to use
        bool needsUpdate = true;
    };

    class MaterialPipeline {
    public:
        explicit MaterialPipeline(core::Device& device, core::SwapChain& swapChain);
        ~MaterialPipeline() = default;

        // Initialize with render pass to use
        void init(vk::RenderPass renderPass, vk::DescriptorSetLayout globalDescriptorLayout);

        // Clean up all resources
        void cleanUp();

        // Compile material to pipeline (creates shader + pipeline if not cached)
        // Returns pipeline hash for binding
        size_t compileMaterial(const ::material::MaterialData& material);

        // Create material instance (UBO + descriptor set)
        // Returns instance ID
        std::string createMaterialInstance(const std::string& materialPath,
                                          const ::material::MaterialData& material);

        // Update material instance parameters
        void updateMaterialInstance(const std::string& instanceId,
                                   const MaterialParameterUBO& parameters);

        // Get compiled pipeline for rendering
        const CompiledMaterialPipeline* getCompiledPipeline(size_t pipelineHash) const;

        // Get material instance for binding
        const MaterialInstance* getMaterialInstance(const std::string& instanceId) const;

        // Bind material for rendering
        void bindMaterial(const vk::CommandBuffer& commandBuffer,
                         const std::string& instanceId,
                         vk::DescriptorSet globalDescriptorSet) const;

        // Get default material pipeline (for fallback)
        const CompiledMaterialPipeline* getDefaultPipeline() const;

        // Remove unused material instances
        void cleanupUnusedInstances();

        // Get material descriptor set layout
        vk::DescriptorSetLayout getMaterialDescriptorSetLayout() const { return materialDescriptorSetLayout; }

    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::RenderPass renderPass;
        vk::DescriptorSetLayout globalDescriptorLayout;
        vk::DescriptorSetLayout materialDescriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        // Pipeline cache (by shader hash)
        mutable std::mutex pipelineMutex;
        std::unordered_map<size_t, CompiledMaterialPipeline> compiledPipelines;

        // Material instances (by material path)
        mutable std::mutex instanceMutex;
        std::unordered_map<std::string, MaterialInstance> materialInstances;

        // Default material for fallback
        size_t defaultPipelineHash = 0;

        // Helpers
        void createMaterialDescriptorSetLayout();
        void createDescriptorPool(uint32_t maxMaterials = 256);

        size_t computeShaderHash(const std::string& vertexShader,
                                const std::string& fragmentShader) const;

        vk::Pipeline createPipeline(const core::Shader& shader,
                                   vk::PipelineLayout layout) const;

        vk::PipelineLayout createPipelineLayout() const;

        void createMaterialUBO(MaterialInstance& instance);
        void createMaterialDescriptorSet(MaterialInstance& instance);
    };

}
