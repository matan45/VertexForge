#include "MaterialPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include "print/Logger.hpp"
#include "../mesh/MeshTypes.hpp"
#include <functional>

namespace render::material {

    MaterialPipeline::MaterialPipeline(core::Device& device, core::SwapChain& swapChain)
        : device{ device }
        , swapChain{ swapChain }
    {
    }

    void MaterialPipeline::init(vk::RenderPass renderPass, vk::DescriptorSetLayout globalDescriptorLayout) {
        this->renderPass = renderPass;
        this->globalDescriptorLayout = globalDescriptorLayout;

        createMaterialDescriptorSetLayout();
        createDescriptorPool();
    }

    void MaterialPipeline::cleanUp() {
        auto& logicalDevice = device.getLogicalDevice();

        // Clean up material instances
        for (auto& [path, instance] : materialInstances) {
            if (instance.parameterUBO) {
                logicalDevice.destroyBuffer(instance.parameterUBO);
            }
            if (instance.parameterUBOMemory) {
                logicalDevice.freeMemory(instance.parameterUBOMemory);
            }
        }
        materialInstances.clear();

        // Clean up compiled pipelines
        for (auto& [hash, pipeline] : compiledPipelines) {
            if (pipeline.pipeline) {
                logicalDevice.destroyPipeline(pipeline.pipeline);
            }
            if (pipeline.pipelineLayout) {
                logicalDevice.destroyPipelineLayout(pipeline.pipelineLayout);
            }
            if (pipeline.shader) {
                pipeline.shader->cleanUp();
            }
        }
        compiledPipelines.clear();

        // Clean up descriptor resources
        if (descriptorPool) {
            logicalDevice.destroyDescriptorPool(descriptorPool);
        }
        if (materialDescriptorSetLayout) {
            logicalDevice.destroyDescriptorSetLayout(materialDescriptorSetLayout);
        }
    }

    size_t MaterialPipeline::compileMaterial(const ::material::MaterialData& material) {
        // Compute hash from shader code
        size_t hash = computeShaderHash(material.cachedVertexShader, material.cachedFragmentShader);

        std::lock_guard<std::mutex> lock(pipelineMutex);

        // Check if already compiled
        auto it = compiledPipelines.find(hash);
        if (it != compiledPipelines.end() && it->second.valid) {
            return hash;
        }

        // Create new compiled pipeline
        CompiledMaterialPipeline compiled;
        compiled.shaderHash = hash;

        // Create shader
        compiled.shader = std::make_shared<core::Shader>(device);
        if (!compiled.shader->compileFromSources(
                material.cachedVertexShader,
                material.cachedFragmentShader,
                material.name)) {
            loggerError("Failed to compile material shader: {}", material.name);
            return 0;
        }

        // Create pipeline layout and pipeline
        compiled.pipelineLayout = createPipelineLayout();
        compiled.pipeline = createPipeline(*compiled.shader, compiled.pipelineLayout);
        compiled.valid = true;

        compiledPipelines[hash] = std::move(compiled);

        // Set as default if first
        if (defaultPipelineHash == 0) {
            defaultPipelineHash = hash;
        }

        return hash;
    }

    std::string MaterialPipeline::createMaterialInstance(const std::string& materialPath,
                                                        const ::material::MaterialData& material) {
        std::lock_guard<std::mutex> lock(instanceMutex);

        // Check if instance already exists
        auto it = materialInstances.find(materialPath);
        if (it != materialInstances.end()) {
            return materialPath;
        }

        MaterialInstance instance;
        instance.materialPath = materialPath;
        instance.pipelineHash = computeShaderHash(material.cachedVertexShader, material.cachedFragmentShader);

        // Create UBO and descriptor set
        createMaterialUBO(instance);
        createMaterialDescriptorSet(instance);

        // Initialize with material's parameters
        MaterialParameterUBO params;
        // Extract parameters from material data
        for (const auto& [name, param] : material.parameters) {
            if (name == "albedo" && std::holds_alternative<glm::vec4>(param.value)) {
                params.albedo = std::get<glm::vec4>(param.value);
            } else if (name == "metallic" && std::holds_alternative<float>(param.value)) {
                params.metallic = std::get<float>(param.value);
            } else if (name == "roughness" && std::holds_alternative<float>(param.value)) {
                params.roughness = std::get<float>(param.value);
            } else if (name == "ao" && std::holds_alternative<float>(param.value)) {
                params.ao = std::get<float>(param.value);
            } else if (name == "emission" && std::holds_alternative<float>(param.value)) {
                params.emission = std::get<float>(param.value);
            }
        }

        // Upload parameters to UBO
        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(instance.parameterUBOMemory, 0, sizeof(MaterialParameterUBO), {}, &data);
        memcpy(data, &params, sizeof(MaterialParameterUBO));
        device.getLogicalDevice().unmapMemory(instance.parameterUBOMemory);

        instance.needsUpdate = false;
        materialInstances[materialPath] = std::move(instance);

        return materialPath;
    }

    void MaterialPipeline::updateMaterialInstance(const std::string& instanceId,
                                                  const MaterialParameterUBO& parameters) {
        std::lock_guard<std::mutex> lock(instanceMutex);

        auto it = materialInstances.find(instanceId);
        if (it == materialInstances.end()) {
            loggerError("Material instance not found: {}", instanceId);
            return;
        }

        auto& instance = it->second;

        // Upload new parameters
        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(instance.parameterUBOMemory, 0, sizeof(MaterialParameterUBO), {}, &data);
        memcpy(data, &parameters, sizeof(MaterialParameterUBO));
        device.getLogicalDevice().unmapMemory(instance.parameterUBOMemory);

        instance.needsUpdate = false;
    }

    const CompiledMaterialPipeline* MaterialPipeline::getCompiledPipeline(size_t pipelineHash) const {
        std::lock_guard<std::mutex> lock(pipelineMutex);

        auto it = compiledPipelines.find(pipelineHash);
        if (it != compiledPipelines.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const MaterialInstance* MaterialPipeline::getMaterialInstance(const std::string& instanceId) const {
        std::lock_guard<std::mutex> lock(instanceMutex);

        auto it = materialInstances.find(instanceId);
        if (it != materialInstances.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void MaterialPipeline::bindMaterial(const vk::CommandBuffer& commandBuffer,
                                       const std::string& instanceId,
                                       vk::DescriptorSet globalDescriptorSet) const {
        const MaterialInstance* instance = getMaterialInstance(instanceId);
        if (!instance) {
            loggerError("Cannot bind material - instance not found: {}", instanceId);
            return;
        }

        const CompiledMaterialPipeline* pipeline = getCompiledPipeline(instance->pipelineHash);
        if (!pipeline || !pipeline->valid) {
            // Try default pipeline
            pipeline = getDefaultPipeline();
            if (!pipeline) {
                loggerError("No valid pipeline for material: {}", instanceId);
                return;
            }
        }

        // Bind pipeline
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->pipeline);

        // Bind descriptor sets (set 0 = global, set 1 = material)
        std::array<vk::DescriptorSet, 2> descriptorSets = {
            globalDescriptorSet,
            instance->descriptorSet
        };
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipeline->pipelineLayout,
            0,
            descriptorSets,
            nullptr
        );
    }

    const CompiledMaterialPipeline* MaterialPipeline::getDefaultPipeline() const {
        if (defaultPipelineHash == 0) {
            return nullptr;
        }
        return getCompiledPipeline(defaultPipelineHash);
    }

    void MaterialPipeline::cleanupUnusedInstances() {
        // This would be called periodically to remove instances no longer in use
        // For now, we keep all instances - implement reference counting later if needed
    }

    void MaterialPipeline::createMaterialDescriptorSetLayout() {
        // Material parameters UBO binding
        vk::DescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        materialDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void MaterialPipeline::createDescriptorPool(uint32_t maxMaterials) {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eUniformBuffer;
        poolSize.descriptorCount = maxMaterials;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = maxMaterials;
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    size_t MaterialPipeline::computeShaderHash(const std::string& vertexShader,
                                              const std::string& fragmentShader) const {
        std::hash<std::string> hasher;
        size_t h1 = hasher(vertexShader);
        size_t h2 = hasher(fragmentShader);
        // Combine hashes
        return h1 ^ (h2 << 1);
    }

    vk::Pipeline MaterialPipeline::createPipeline(const core::Shader& shader,
                                                 vk::PipelineLayout layout) const {
        // Vertex input
        auto bindingDescription = mesh::MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = mesh::MeshVertexInput::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        // Input assembly
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport state (dynamic)
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterizer
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth stencil
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Dynamic state
        std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Create pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(shader.getShaderStages().size());
        pipelineInfo.pStages = shader.getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            loggerError("Failed to create material graphics pipeline");
            return nullptr;
        }

        return result.value;
    }

    vk::PipelineLayout MaterialPipeline::createPipelineLayout() const {
        // Push constant for model matrix
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(glm::mat4);  // Just model matrix

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            globalDescriptorLayout,
            materialDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        return device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void MaterialPipeline::createMaterialUBO(MaterialInstance& instance) {
        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = sizeof(MaterialParameterUBO);
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        instance.parameterUBO = device.getLogicalDevice().createBuffer(bufferInfo);

        vk::MemoryRequirements memRequirements = device.getLogicalDevice().getBufferMemoryRequirements(instance.parameterUBO);

        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        instance.parameterUBOMemory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindBufferMemory(instance.parameterUBO, instance.parameterUBOMemory, 0);
    }

    void MaterialPipeline::createMaterialDescriptorSet(MaterialInstance& instance) {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &materialDescriptorSetLayout;

        instance.descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        // Update descriptor with UBO
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = instance.parameterUBO;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialParameterUBO);

        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.dstSet = instance.descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(descriptorWrite, nullptr);
    }

}
