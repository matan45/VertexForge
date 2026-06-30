#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <unordered_map>
#include <memory>
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
        std::string materialIRHash;
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

    public:
        explicit MaterialShaderCache(core::Device& device);
        ~MaterialShaderCache();

        void init(vk::PipelineLayout pipelineLayout,
                  vk::Extent2D swapchainExtent,
                  vk::Format colorFormat,
                  vk::Format depthFormat);
        
        const MaterialPipelineData* getOrCreatePipeline(const std::string& materialPath,
                                                        const material::MaterialData& materialData);
        
        void invalidate(const std::string& materialPath);
        
        void invalidateAll();
        
        void cleanUp();
        
        bool hasPipeline(const std::string& materialPath) const;
        
        const std::string& getLastCompilationError() const { return lastCompilationError; }

    private:
        bool compileAndCreatePipeline(const std::string& materialPath,
                                      const material::MaterialData& materialData,
                                      MaterialPipelineData& outData);
        
        bool createPipelines(MaterialPipelineData& data);
        
        static std::string hashShaderSource(const std::string& source);
    };
}
