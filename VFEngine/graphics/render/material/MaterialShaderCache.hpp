#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <memory>

namespace core {
    class Device;
    class Shader;
}

namespace material {
    struct MaterialData;
}

namespace render::mesh
{
    // Cached compiled shader and pipeline for a material
    struct MaterialPipelineData {
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline opaquePipeline;
        vk::Pipeline maskedPipeline;
        std::string vertexShaderHash;
        std::string fragmentShaderHash;
        bool valid = false;
    };

    // Cache for material shaders and pipelines
    class MaterialShaderCache {
    public:
        explicit MaterialShaderCache(core::Device& device);
        ~MaterialShaderCache();

        // Initialize with shared resources from StaticMeshPipeline
        void init(vk::RenderPass renderPass,
                  vk::PipelineLayout pipelineLayout,
                  vk::Extent2D swapchainExtent);

        // Get or create pipeline for a material
        // Returns nullptr if material has no custom shader or compilation fails
        const MaterialPipelineData* getOrCreatePipeline(const std::string& materialPath,
                                                         const material::MaterialData& materialData);

        // Invalidate cached pipeline for a material (called when material is modified)
        void invalidate(const std::string& materialPath);

        // Invalidate all cached pipelines
        void invalidateAll();

        // Clean up all resources
        void cleanUp();

        // Check if a material has a valid cached pipeline
        bool hasPipeline(const std::string& materialPath) const;

        // Get last compilation error (empty if no error)
        const std::string& getLastCompilationError() const { return lastCompilationError; }

    private:
        std::string lastCompilationError;
        core::Device& device;
        vk::RenderPass renderPass;
        vk::PipelineLayout pipelineLayout;
        vk::Extent2D swapchainExtent;
        bool initialized = false;

        // Cache: material path -> pipeline data
        std::unordered_map<std::string, MaterialPipelineData> cache;

        // Compile shader and create pipeline
        bool compileAndCreatePipeline(const std::string& materialPath,
                                      const material::MaterialData& materialData,
                                      MaterialPipelineData& outData);

        // Create pipeline from compiled shader
        bool createPipelines(MaterialPipelineData& data);

        // Generate hash for shader source (for change detection)
        static std::string hashShaderSource(const std::string& source);
    };
}
