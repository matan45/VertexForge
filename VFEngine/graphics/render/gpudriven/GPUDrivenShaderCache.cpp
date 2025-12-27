#include "GPUDrivenShaderCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/SwapChain.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <array>
#include <functional>
#include <regex>
#include <spdlog/spdlog.h>

namespace render::gpudriven {

    GPUDrivenShaderCache::GPUDrivenShaderCache(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
        // Group 0 is always active (default PBR)
        activeGroups.insert(0);
    }

    GPUDrivenShaderCache::~GPUDrivenShaderCache()
    {
        cleanup();
    }

    void GPUDrivenShaderCache::init(vk::DescriptorSetLayout iblLayout,
                                     vk::DescriptorSetLayout perDrawDataLayout,
                                     vk::DescriptorSetLayout bindlessTextureLayout,
                                     vk::RenderPass renderPass)
    {
        this->iblLayout = iblLayout;
        this->perDrawDataLayout = perDrawDataLayout;
        this->bindlessTextureLayout = bindlessTextureLayout;
        this->renderPass = renderPass;

        // Create shared pipeline layout for all custom shaders
        // Same layout as default GPU-driven pipeline:
        // Set 0: IBL (camera UBO + irradiance + prefilter + brdfLUT)
        // Set 1: Per-draw data storage buffer
        // Set 2: Bindless textures
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            iblLayout,
            perDrawDataLayout,
            bindlessTextureLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
        initialized = true;

        spdlog::info("GPUDrivenShaderCache: Initialized with shared pipeline layout");
    }

    uint32_t GPUDrivenShaderCache::getOrCreateShaderGroup(const std::string& materialPath,
                                                           const material::MaterialData& materialData)
    {
        if (!initialized) {
            return 0;  // Default group
        }

        // Fast path: check cache first before doing expensive checks
        auto groupIt = materialToGroup.find(materialPath);
        if (groupIt != materialToGroup.end()) {
            // Already have a cached group for this material
            return groupIt->second;
        }

        // Check if material has custom shaders
        // Only consider it custom if BOTH vertex and fragment shaders are present
        if (materialData.cachedVertexShader.empty() || materialData.cachedFragmentShader.empty()) {
            return 0;  // No custom shader, use default pipeline
        }

        // Check if the shader graph contains nodes that require custom shader handling
        // (e.g., Time node for animations, custom functions, etc.)
        bool hasCustomLogic = false;
        for (const auto& node : materialData.graph.nodes) {
            if (node.type == material::NodeType::Time) {
                hasCustomLogic = true;
                break;
            }
            // Add other custom node types as needed
        }

        if (!hasCustomLogic) {
            // Material has cached shaders but they're just standard PBR, use default pipeline
            // Cache this result so we don't check again
            materialToGroup[materialPath] = 0;
            return 0;
        }

        // Create new pipeline
        // Determine target group index BEFORE compilation (for shader group filtering)
        uint32_t targetGroupIndex = nextGroupIndex;
        if (targetGroupIndex >= MAX_SHADER_GROUPS) {
            spdlog::warn("GPUDrivenShaderCache: Max shader groups reached ({}), reusing last group", MAX_SHADER_GROUPS);
            targetGroupIndex = MAX_SHADER_GROUPS - 1;
        }

        GPUDrivenPipelineData data;
        if (compileAndCreatePipeline(materialPath, materialData, targetGroupIndex, data)) {
            // Assign group index and increment counter
            data.groupIndex = targetGroupIndex;
            if (nextGroupIndex < MAX_SHADER_GROUPS - 1) {
                nextGroupIndex++;
            }

            cache[materialPath] = std::move(data);
            materialToGroup[materialPath] = cache[materialPath].groupIndex;

            spdlog::info("GPUDrivenShaderCache: Created shader group {} for material: {}",
                         cache[materialPath].groupIndex, materialPath);

            return cache[materialPath].groupIndex;
        }

        spdlog::warn("GPUDrivenShaderCache: Failed to compile custom shader for: {}, using default", materialPath);
        return 0;  // Fall back to default on failure
    }

    vk::Pipeline GPUDrivenShaderCache::getPipeline(uint32_t shaderGroup, bool masked) const
    {
        if (shaderGroup == 0) {
            return nullptr;  // Caller should use default pipeline
        }

        // Find pipeline by group index
        for (const auto& [path, data] : cache) {
            if (data.groupIndex == shaderGroup && data.valid) {
                return masked ? data.maskedPipeline : data.opaquePipeline;
            }
        }

        return nullptr;
    }

    bool GPUDrivenShaderCache::isValidGroup(uint32_t group) const
    {
        if (group == 0) {
            return true;  // Default group is always valid
        }

        for (const auto& [path, data] : cache) {
            if (data.groupIndex == group && data.valid) {
                return true;
            }
        }

        return false;
    }

    bool GPUDrivenShaderCache::compileAndCreatePipeline(const std::string& materialPath,
                                                         const material::MaterialData& materialData,
                                                         uint32_t targetGroupIndex,
                                                         GPUDrivenPipelineData& outData)
    {
        // Generate GPU-driven compatible vertex shader
        std::string adaptedVertexShader = generateGPUDrivenVertexShader();

        // Generate GPU-driven compatible fragment shader with group filtering
        std::string adaptedFragmentShader = generateGPUDrivenFragmentShader(targetGroupIndex);

        // Create shader object
        outData.shader = std::make_shared<core::Shader>(device);

        // Compile from adapted sources
        bool success = outData.shader->compileFromSources(
            adaptedVertexShader,
            adaptedFragmentShader,
            materialData.name + "_gpudriven_g" + std::to_string(targetGroupIndex)
        );

        if (!success || outData.shader->getShaderStages().empty()) {
            lastCompilationError = outData.shader->getLastCompilationError();
            spdlog::error("GPUDrivenShaderCache: Failed to compile shader: {} - {}",
                          materialPath, lastCompilationError);
            return false;
        }

        // Clear error on success
        lastCompilationError.clear();

        // Store hashes for change detection
        outData.vertexShaderHash = hashShaderSource(materialData.cachedVertexShader);
        outData.fragmentShaderHash = hashShaderSource(materialData.cachedFragmentShader);

        // Create pipelines
        if (!createPipelines(outData)) {
            spdlog::error("GPUDrivenShaderCache: Failed to create pipeline for: {}", materialPath);
            return false;
        }

        outData.valid = true;
        return true;
    }

    bool GPUDrivenShaderCache::createPipelines(GPUDrivenPipelineData& data)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Vertex input state - using MeshVertexInput helper
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

        // Viewport and scissor
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

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

        // Depth testing
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending - opaque
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

        // Create opaque pipeline
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(data.shader->getShaderStages().size());
        pipelineInfo.pStages = data.shader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            spdlog::error("GPUDrivenShaderCache: Failed to create opaque pipeline");
            return false;
        }
        data.opaquePipeline = result.value;

        // Masked pipeline (same as opaque for now)
        result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            spdlog::error("GPUDrivenShaderCache: Failed to create masked pipeline");
            return false;
        }
        data.maskedPipeline = result.value;

        return true;
    }

    std::string GPUDrivenShaderCache::generateGPUDrivenVertexShader() const
    {
        return R"(#version 460 core

// GPU-Driven custom vertex shader

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out flat uint fragDrawIndex;

// Set 0: Camera UBO (matches GPU-driven layout)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Per-draw data structure (must match GPUDrivenTypes.hpp PerDrawData)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
    uint lodLevel;
    uint shaderGroupIndex;
    uint padding1;
    uint padding2;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

void main() {
    // Get draw index from gl_BaseInstance (set by indirect draw command)
    uint drawIndex = gl_BaseInstance;
    PerDrawData drawData = perDrawData[drawIndex];

    vec4 worldPos = drawData.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal using pre-computed normal matrix
    fragNormal = normalize(mat3(drawData.normalMatrix) * inNormal);

    fragTexCoord = inTexCoord;
    fragDrawIndex = drawIndex;

    gl_Position = camera.projection * camera.view * worldPos;
}
)";
    }

    std::string GPUDrivenShaderCache::generateGPUDrivenFragmentShader(uint32_t expectedGroupIndex) const
    {
        // Build GPU-driven fragment shader for custom materials
        // Note: No shader group filtering needed - compute shader outputs to separate
        // buffer sections per (batch, shaderGroup), so each pipeline only receives
        // draw commands for its own shader group.
        std::string shader = R"(#version 460 core

// GPU-Driven custom fragment shader (group )" + std::to_string(expectedGroupIndex) + R"()

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;

layout(location = 0) out vec4 outColor;

// Set 0: Camera and IBL resources
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Per-draw data structure (must match GPUDrivenTypes.hpp PerDrawData)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 materialParams;
    uvec4 textureIndices0;
    uvec4 textureIndices1;
    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;
    uint lodLevel;
    uint shaderGroupIndex;
    uint padding1;
    uint padding2;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 2: Bindless texture array
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

void main() {
    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Get texture indices
    uint albedoIdx = drawData.textureIndices0.x;

    // Apply time-based UV animation (for materials with Time node)
    // This scrolls the texture based on time
    vec2 uv = fragTexCoord + vec2(camera.u_Time * 0.1, 0.0);

    // Sample albedo (texture is in sRGB space)
    vec4 albedoSample = vec4(drawData.albedo.rgb, 1.0);
    if (isValidTexture(albedoIdx)) {
        albedoSample = texture(bindlessTextures[nonuniformEXT(albedoIdx)], uv);
    }

    // Output texture color directly (already in sRGB space)
    vec3 color = albedoSample.rgb;

    outColor = vec4(color, albedoSample.a);
}
)";
        return shader;
    }

    void GPUDrivenShaderCache::invalidate(const std::string& materialPath)
    {
        auto it = cache.find(materialPath);
        if (it != cache.end()) {
            device.getLogicalDevice().waitIdle();

            if (it->second.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(it->second.opaquePipeline);
            if (it->second.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(it->second.maskedPipeline);

            if (it->second.shader)
                it->second.shader->cleanUp();

            // Remove from group mapping
            materialToGroup.erase(materialPath);

            cache.erase(it);
            spdlog::info("GPUDrivenShaderCache: Invalidated cache for: {}", materialPath);
        }
    }

    void GPUDrivenShaderCache::invalidateAll()
    {
        if (cache.empty()) return;

        device.getLogicalDevice().waitIdle();

        for (auto& [path, data] : cache) {
            if (data.opaquePipeline)
                device.getLogicalDevice().destroyPipeline(data.opaquePipeline);
            if (data.maskedPipeline)
                device.getLogicalDevice().destroyPipeline(data.maskedPipeline);

            if (data.shader)
                data.shader->cleanUp();
        }

        cache.clear();
        materialToGroup.clear();
        nextGroupIndex = 1;  // Reset group allocation

        spdlog::info("GPUDrivenShaderCache: Invalidated all caches");
    }

    void GPUDrivenShaderCache::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        bool renderPassChanged = (renderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && iblLayout != newIBLLayout);

        if (renderPassChanged || iblLayoutChanged) {
            spdlog::info("GPUDrivenShaderCache: Updating render pass/IBL layout, invalidating cached pipelines");

            // Invalidate all cached pipelines
            invalidateAll();

            // Destroy and recreate shared pipeline layout if IBL layout changed
            if (iblLayoutChanged && pipelineLayout) {
                device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);

                std::array<vk::DescriptorSetLayout, 3> setLayouts = {
                    newIBLLayout,
                    perDrawDataLayout,
                    bindlessTextureLayout
                };

                vk::PipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.data();
                layoutInfo.pushConstantRangeCount = 0;
                layoutInfo.pPushConstantRanges = nullptr;

                pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
                iblLayout = newIBLLayout;
            }

            renderPass = newRenderPass;
        }
    }

    void GPUDrivenShaderCache::cleanup()
    {
        if (!initialized) return;

        invalidateAll();

        if (pipelineLayout) {
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        initialized = false;
    }

    std::string GPUDrivenShaderCache::hashShaderSource(const std::string& source)
    {
        std::hash<std::string> hasher;
        return std::to_string(hasher(source));
    }

}
