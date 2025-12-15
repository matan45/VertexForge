#include "StaticMeshPipeline.hpp"
#include "MeshGPUCache.hpp"
#include "MaterialTextureCache.hpp"
#include "MaterialShaderCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Utilities.hpp"
#include "resource/MeshResource.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialManager.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <optional>
#include <algorithm>
#include <cmath>

namespace render::mesh
{
    // Helper struct to hold extracted PBR values from a material
    struct ExtractedPBRValues
    {
        glm::vec4 albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        material::BlendMode blendMode = material::BlendMode::Opaque;

        // Texture paths (empty = use scalar value)
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;

        // Material path for shader cache lookup
        std::string materialPath;
    };

    // Forward declaration for recursive evaluation
    static std::optional<float> evaluateFloatValue(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time);

    // Helper to get the input value for a node's pin (recursively evaluates connected nodes)
    static std::optional<float> getInputFloat(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == nodeId && link.targetPin == pinName) {
                return evaluateFloatValue(graph, link.sourceNodeId, link.sourcePin, time);
            }
        }
        return std::nullopt;
    }

    // Evaluate a node and return its float output value
    static std::optional<float> evaluateFloatValue(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type) {
            case material::NodeType::ConstantScalar: {
                auto it = node->properties.find("value");
                if (it != node->properties.end() && std::holds_alternative<float>(it->second)) {
                    return std::get<float>(it->second);
                }
                break;
            }
            case material::NodeType::Time: {
                return time;
            }
            case material::NodeType::Sin: {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) {
                    return std::sin(*inputVal);
                }
                return 0.0f;
            }
            case material::NodeType::Cos: {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) {
                    return std::cos(*inputVal);
                }
                return 0.0f;
            }
            case material::NodeType::Multiply: {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                float aVal = a.value_or(1.0f);
                float bVal = b.value_or(1.0f);
                return aVal * bVal;
            }
            case material::NodeType::Add: {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                float aVal = a.value_or(0.0f);
                float bVal = b.value_or(0.0f);
                return aVal + bVal;
            }
            default:
                break;
        }
        return std::nullopt;
    }

    // Helper to get value from a node connected to a specific pin (static values only)
    static std::optional<material::NodeProperty> getConnectedValue(
        const material::ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        // Find link to this pin
        for (const auto& link : graph.links) {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName) {
                // Found a connection - get the source node
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                // Check if it's a constant node and get its value
                switch (sourceNode->type) {
                    case material::NodeType::ConstantScalar: {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end()) {
                            return it->second;
                        }
                        break;
                    }
                    case material::NodeType::ConstantVec2:
                    case material::NodeType::ConstantVec3:
                    case material::NodeType::ConstantColor: {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end()) {
                            return it->second;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        return std::nullopt;
    }

    // Helper to evaluate EmissionStrength dynamically (supports Time, Sin, Cos nodes)
    static float evaluateEmissionStrength(
        const material::ShaderGraph& graph,
        uint32_t outputNodeId,
        float time)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == outputNodeId && link.targetPin == "EmissionStrength") {
                auto val = evaluateFloatValue(graph, link.sourceNodeId, link.sourcePin, time);
                if (val) {
                    return *val;
                }
            }
        }
        return 0.0f;
    }

    // Helper to get texture path from a TextureSample node connected to a specific pin
    static std::string getConnectedTexturePath(
        const material::ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName) {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == material::NodeType::TextureSample) {
                    auto it = sourceNode->properties.find("texturePath");
                    if (it != sourceNode->properties.end() &&
                        std::holds_alternative<std::string>(it->second)) {
                        return std::get<std::string>(it->second);
                    }
                }
            }
        }
        return "";
    }

    // Extract PBR values from a loaded MaterialData by traversing the shader graph
    static ExtractedPBRValues extractPBRFromMaterial(const material::MaterialData& matData)
    {
        ExtractedPBRValues pbr;

        // Find PBR Output node
        const auto* outputNode = matData.graph.findOutputNode();
        if (!outputNode) {
            return pbr;  // Return defaults if no output node
        }

        // Try to get Albedo from connected node
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Albedo")) {
            if (std::holds_alternative<glm::vec4>(*val)) {
                pbr.albedo = std::get<glm::vec4>(*val);
            } else if (std::holds_alternative<glm::vec3>(*val)) {
                glm::vec3 rgb = std::get<glm::vec3>(*val);
                pbr.albedo = glm::vec4(rgb, 1.0f);
            }
        }

        // Try to get Metallic
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Metallic")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.metallic = std::get<float>(*val);
            }
        }

        // Try to get Roughness
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Roughness")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.roughness = std::get<float>(*val);
            }
        }

        // Try to get AO
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "AO")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.ao = std::get<float>(*val);
            }
        }

        // Try to get Emission (Vec3) and EmissionStrength (Float)
        float emissionStrength = 0.0f;
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "EmissionStrength")) {
            if (std::holds_alternative<float>(*val)) {
                emissionStrength = std::get<float>(*val);
            }
        }
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Emission")) {
            if (std::holds_alternative<float>(*val)) {
                // Single float emission value
                pbr.emission = std::get<float>(*val) * emissionStrength;
            } else if (std::holds_alternative<glm::vec3>(*val)) {
                // Vec3 emission - use luminance approximation
                glm::vec3 emissionColor = std::get<glm::vec3>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) * emissionStrength;
            } else if (std::holds_alternative<glm::vec4>(*val)) {
                // Vec4 emission - use luminance approximation
                glm::vec4 emissionColor = std::get<glm::vec4>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) * emissionStrength;
            }
        } else if (emissionStrength > 0.0f) {
            // No emission color connected but strength is set - use white emission
            pbr.emission = emissionStrength;
        }

        // Try to get Opacity
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Opacity")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.albedo.a = std::get<float>(*val);
            }
        }

        // Also check exposed parameters as fallback
        auto findParam = [&matData](const std::string& name) -> const material::MaterialParameter* {
            auto it = matData.parameters.find(name);
            return (it != matData.parameters.end()) ? &it->second : nullptr;
        };

        // Fallback to parameters if graph values not found
        if (pbr.albedo == glm::vec4(1.0f)) {
            if (const auto* param = findParam("Albedo")) {
                if (std::holds_alternative<glm::vec4>(param->value)) {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            } else if (const auto* param = findParam("BaseColor")) {
                if (std::holds_alternative<glm::vec4>(param->value)) {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            }
        }

        // Extract texture paths from connected TextureSample nodes
        pbr.albedoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Albedo");
        pbr.metallicTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Metallic");
        pbr.roughnessTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Roughness");
        pbr.aoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "AO");
        pbr.normalTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Normal");
        pbr.emissionTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Emission");

        // Get blend mode from material
        pbr.blendMode = matData.blendMode;

        return pbr;
    }

    // Get PBR values for a submesh, checking material assignments in order:
    // 1. Per-submesh material override
    // 2. Default material for the mesh
    // 3. Fallback defaults from MeshRenderData
    static ExtractedPBRValues getPBRForSubmesh(
        const MeshRenderData& meshData,
        const std::string& submeshName,
        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache,
        float time = 0.0f)
    {
        ExtractedPBRValues pbr;
        pbr.albedo = meshData.albedo;
        pbr.metallic = meshData.metallic;
        pbr.roughness = meshData.roughness;
        pbr.ao = meshData.ao;
        pbr.emission = meshData.emission;

        std::string materialPath;

        // Check for per-submesh material override
        const auto* submeshMat = meshData.getMaterialForSubmesh(submeshName);
        if (submeshMat && !submeshMat->materialPath.empty()) {
            materialPath = submeshMat->materialPath;
        }
        // Fall back to default material
        else if (!meshData.defaultMaterialPath.empty()) {
            materialPath = meshData.defaultMaterialPath;
        }

        // Load and extract PBR values from the material (using cache)
        if (!materialPath.empty()) {
            std::shared_ptr<material::MaterialData> matData;

            // Check cache first
            auto cacheIt = matCache.find(materialPath);
            if (cacheIt != matCache.end() && cacheIt->second) {
                matData = cacheIt->second;
                pbr = extractPBRFromMaterial(*matData);
            } else {
                // Load and cache the material
                matData = material::MaterialManager::instance().loadMaterial(materialPath);
                if (matData) {
                    matCache[materialPath] = matData;
                    pbr = extractPBRFromMaterial(*matData);
                }
            }

            // Evaluate dynamic emission strength (Time, Sin, Cos nodes)
            if (matData) {
                const auto* outputNode = matData->graph.findOutputNode();
                if (outputNode) {
                    float dynamicEmission = evaluateEmissionStrength(matData->graph, outputNode->id, time);
                    if (dynamicEmission != 0.0f) {
                        pbr.emission = dynamicEmission;
                    }
                }
            }

            // Store material path for shader cache lookup
            pbr.materialPath = materialPath;
        }

        return pbr;
    }

    StaticMeshPipeline::StaticMeshPipeline(core::Device& device, core::SwapChain& swapChain,
                                           core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        // Create command pool for buffer upload operations
        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        commandPoolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        commandPool = device.getLogicalDevice().createCommandPoolUnique(commandPoolInfo);

        // Create mesh GPU cache for mesh buffer management
        meshCache = std::make_unique<MeshGPUCache>(device);

        // Create material texture cache for texture GPU resources
        textureCache = std::make_unique<MaterialTextureCache>(device);
        textureCache->init(commandPool.get());

        // Create material shader cache for per-material pipeline compilation
        materialShaderCache = std::make_unique<MaterialShaderCache>(device);
    }

    StaticMeshPipeline::~StaticMeshPipeline() = default;

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();  // Set 1 layout
        createDescriptorPool();
        createTextureDescriptorPool();       // Set 1 pool
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();  // Initialize set 1 with defaults
        createFramebuffers();
        createWireframePipeline();
        createAABBBuffers();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createTextureDescriptorSetLayout();  // Set 1 layout
        createDescriptorPool();
        createTextureDescriptorPool();       // Set 1 pool
        createCameraUBO();
        createDefaultIBLTextures();
        createDescriptorSet(defaultIrradiance, defaultPrefilter, defaultBrdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        materialShaderCache->init(renderPass, pipelineLayout, swapChain.getSwapchainExtent());
        initializeDefaultTextureDescriptors();  // Initialize set 1 with defaults
        createFramebuffers();
        createWireframePipeline();
        createAABBBuffers();
        usingDefaultTextures = true;
    }

    void StaticMeshPipeline::loadShaders()
    {
        meshShader = std::make_shared<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/mesh/mesh.glsl");

        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/mesh/wireframe.glsl");
    }

    void StaticMeshPipeline::createDefaultIBLTextures()
    {
        // Create simple 1x1 cubemap textures with neutral values for fallback PBR lighting
        const uint32_t size = 1;
        const uint32_t mipLevels = 1;
        
        // Face order: +X, -X, +Y (top), -Y (bottom), +Z, -Z
        auto createCubemap = [&](ibl::ImageData& imageData, const std::array<std::array<float, 4>, 6>& faceColors) {
            // Create image
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{size, size, 1};
            imageInfo.mipLevels = mipLevels;
            imageInfo.arrayLayers = 6;  // Cubemap
            imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;
            imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

            imageData.image = device.getLogicalDevice().createImage(imageInfo);

            // Allocate memory
            vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
                memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
            device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

            // Create staging buffer with color data for all 6 faces
            std::vector<float> pixels(6 * 4);  // 6 faces * 4 components (RGBA)
            for (int i = 0; i < 6; i++) {
                pixels[i * 4 + 0] = faceColors[i][0];
                pixels[i * 4 + 1] = faceColors[i][1];
                pixels[i * 4 + 2] = faceColors[i][2];
                pixels[i * 4 + 3] = faceColors[i][3];
            }

            vk::DeviceSize imageSize = pixels.size() * sizeof(float);
            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingMemory;

            core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            stagingRequest.size = imageSize;
            stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
            stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

            void* data;
            [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            memcpy(data, pixels.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingMemory);

            // Transition image layout and copy data
            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool.get();
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            // Transition to transfer destination
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageData.image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 6;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                {}, nullptr, nullptr, barrier);

            // Copy buffer to image (all 6 faces)
            std::vector<vk::BufferImageCopy> copyRegions(6);
            for (uint32_t face = 0; face < 6; face++) {
                copyRegions[face].bufferOffset = face * 4 * sizeof(float);
                copyRegions[face].bufferRowLength = 0;
                copyRegions[face].bufferImageHeight = 0;
                copyRegions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copyRegions[face].imageSubresource.mipLevel = 0;
                copyRegions[face].imageSubresource.baseArrayLayer = face;
                copyRegions[face].imageSubresource.layerCount = 1;
                copyRegions[face].imageOffset = vk::Offset3D{0, 0, 0};
                copyRegions[face].imageExtent = vk::Extent3D{size, size, 1};
            }
            cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

            // Transition to shader read
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, nullptr, nullptr, barrier);

            cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.getGraphicsQueue().submit(submitInfo);
            device.getGraphicsQueue().waitIdle();

            device.getLogicalDevice().freeCommandBuffers(commandPool.get(), cmd);
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingMemory);

            // Create image view
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = imageData.image;
            viewInfo.viewType = vk::ImageViewType::eCube;
            viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = mipLevels;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 6;
            imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

            // Create sampler - use nearest filtering for 1x1 cubemap to prevent face blending
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eNearest;
            samplerInfo.minFilter = vk::Filter::eNearest;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = static_cast<float>(mipLevels);
            samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
        };

        // Create 2D texture for BRDF LUT (not a cubemap)
        auto create2DTexture = [&](ibl::ImageData& imageData) {
            vk::ImageCreateInfo imageInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.extent = vk::Extent3D{size, size, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;
            imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;

            imageData.image = device.getLogicalDevice().createImage(imageInfo);

            vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
            vk::MemoryAllocateInfo allocInfo{};
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
                memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
            device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

            // Default BRDF LUT value: (scale=1.0, bias=0.0) for specular = prefilteredColor * F
            std::array<float, 4> pixel = {1.0f, 0.0f, 0.0f, 1.0f};
            vk::DeviceSize imageSize = sizeof(pixel);
            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingMemory;

            core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            stagingRequest.size = imageSize;
            stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
            stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

            void* data;
            [[maybe_unused]] auto mapResult2 = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            memcpy(data, pixel.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingMemory);

            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = commandPool.get();
            cmdAllocInfo.commandBufferCount = 1;
            auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
            vk::CommandBuffer cmd = cmdBuffers[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);

            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageData.image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                {}, nullptr, nullptr, barrier);

            vk::BufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = vk::Extent3D{size, size, 1};
            cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, nullptr, nullptr, barrier);

            cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.getGraphicsQueue().submit(submitInfo);
            device.getGraphicsQueue().waitIdle();

            device.getLogicalDevice().freeCommandBuffers(commandPool.get(), cmd);
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingMemory);

            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = imageData.image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.mipLodBias = 0.0f;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 1.0f;
            samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
            imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
        };

        // Completely uniform IBL for smooth shading - no cubemap face boundaries visible
        // All faces identical to eliminate any banding from face transitions
        std::array<std::array<float, 4>, 6> studioIrradiance = {{
            {0.8f, 0.8f, 0.85f, 1.0f},   // +X
            {0.8f, 0.8f, 0.85f, 1.0f},   // -X
            {0.8f, 0.8f, 0.85f, 1.0f},   // +Y
            {0.8f, 0.8f, 0.85f, 1.0f},   // -Y
            {0.8f, 0.8f, 0.85f, 1.0f},   // +Z
            {0.8f, 0.8f, 0.85f, 1.0f}    // -Z
        }};

        std::array<std::array<float, 4>, 6> studioPrefilter = {{
            {0.6f, 0.6f, 0.65f, 1.0f},   // +X
            {0.6f, 0.6f, 0.65f, 1.0f},   // -X
            {0.6f, 0.6f, 0.65f, 1.0f},   // +Y
            {0.6f, 0.6f, 0.65f, 1.0f},   // -Y
            {0.6f, 0.6f, 0.65f, 1.0f},   // +Z
            {0.6f, 0.6f, 0.65f, 1.0f}    // -Z
        }};

        // Irradiance: studio ambient lighting
        createCubemap(defaultIrradiance, studioIrradiance);
        // Prefilter: studio reflections
        createCubemap(defaultPrefilter, studioPrefilter);
        // BRDF LUT: 2D texture
        create2DTexture(defaultBrdfLUT);
    }

    void StaticMeshPipeline::recreate()
    {
        // Cleanup framebuffers and render pass for recreation
        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipeline(maskedPipeline);
        device.getLogicalDevice().destroyPipeline(translucentPipeline);

        createRenderPass();
        createGraphicsPipeline();
        createFramebuffers();
    }

    void StaticMeshPipeline::createRenderPass()
    {
        // Color attachment - load existing content (preserve skybox)
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Preserve skybox
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Depth attachment
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void StaticMeshPipeline::createDescriptorSetLayout()
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

        // Binding 0: Camera UBO (vertex + fragment)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: Irradiance cubemap (fragment only)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Prefilter cubemap (fragment only)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[2].pImmutableSamplers = nullptr;

        // Binding 3: BRDF LUT (fragment only)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[3].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[1].descriptorCount = 3;  // irradiance, prefilter, brdfLUT

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::createCameraUBO()
    {
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(CameraUBO);
        core::Utilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void StaticMeshPipeline::createDescriptorSet(const ibl::ImageData& irradianceMap,
                                                  const ibl::ImageData& prefilterMap,
                                                  const ibl::ImageData& brdfLUT)
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        // Camera UBO binding
        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = cameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(CameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = descriptorSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        // Irradiance map binding
        vk::DescriptorImageInfo irradianceImageInfo{};
        irradianceImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        irradianceImageInfo.imageView = irradianceMap.imageView;
        irradianceImageInfo.sampler = irradianceMap.sampler;

        vk::WriteDescriptorSet irradianceWrite{};
        irradianceWrite.dstSet = descriptorSet;
        irradianceWrite.dstBinding = 1;
        irradianceWrite.dstArrayElement = 0;
        irradianceWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        irradianceWrite.descriptorCount = 1;
        irradianceWrite.pImageInfo = &irradianceImageInfo;

        // Prefilter map binding
        vk::DescriptorImageInfo prefilterImageInfo{};
        prefilterImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        prefilterImageInfo.imageView = prefilterMap.imageView;
        prefilterImageInfo.sampler = prefilterMap.sampler;

        vk::WriteDescriptorSet prefilterWrite{};
        prefilterWrite.dstSet = descriptorSet;
        prefilterWrite.dstBinding = 2;
        prefilterWrite.dstArrayElement = 0;
        prefilterWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        prefilterWrite.descriptorCount = 1;
        prefilterWrite.pImageInfo = &prefilterImageInfo;

        // BRDF LUT binding
        vk::DescriptorImageInfo brdfImageInfo{};
        brdfImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        brdfImageInfo.imageView = brdfLUT.imageView;
        brdfImageInfo.sampler = brdfLUT.sampler;

        vk::WriteDescriptorSet brdfWrite{};
        brdfWrite.dstSet = descriptorSet;
        brdfWrite.dstBinding = 3;
        brdfWrite.dstArrayElement = 0;
        brdfWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        brdfWrite.descriptorCount = 1;
        brdfWrite.pImageInfo = &brdfImageInfo;

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {
            uboWrite, irradianceWrite, prefilterWrite, brdfWrite
        };
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);
    }

    void StaticMeshPipeline::createTextureDescriptorSetLayout()
    {
        // Set 1, Binding 0: Array of 8 material textures
        vk::DescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureBinding.descriptorCount = 8;  // Array of 8 textures
        textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        textureBinding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;

        textureDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createTextureDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 8;  // 8 textures

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        textureDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::initializeDefaultTextureDescriptors()
    {
        // Initialize texture descriptor set with all default textures
        updateTextureDescriptors(textureCache->getImageViews(), textureCache->getSamplers());
    }

    void StaticMeshPipeline::updateTextureDescriptors(
        const std::array<vk::ImageView, 8>& imageViews,
        const std::array<vk::Sampler, 8>& samplers)
    {
        // Allocate descriptor set if not already allocated
        if (!textureDescriptorSet)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureDescriptorSetLayout;
            textureDescriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];
        }

        // Update all 8 texture bindings
        std::array<vk::DescriptorImageInfo, 8> imageInfos;
        for (int i = 0; i < 8; ++i)
        {
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos[i].imageView = imageViews[i];
            imageInfos[i].sampler = samplers[i];
        }

        vk::WriteDescriptorSet writeSet{};
        writeSet.dstSet = textureDescriptorSet;
        writeSet.dstBinding = 0;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeSet.descriptorCount = 8;
        writeSet.pImageInfo = imageInfos.data();

        device.getLogicalDevice().updateDescriptorSets(writeSet, nullptr);
        textureDescriptorsInitialized = true;
    }

    void StaticMeshPipeline::createPipelineLayout()
    {
        // Push constant range for MeshPushConstants
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshPushConstants);

        // Two descriptor set layouts: set 0 (camera + IBL), set 1 (material textures)
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            descriptorSetLayout,        // Set 0: Camera + IBL
            textureDescriptorSetLayout  // Set 1: Material textures
        };

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
    }

    void StaticMeshPipeline::createGraphicsPipeline()
    {
        // Vertex input state - using MeshVertexInput helper
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

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

        // Rasterizer - back-face culling enabled for meshes
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

        // Depth testing - enabled for meshes
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending - no blending
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
        pipelineInfo.stageCount = static_cast<uint32_t>(meshShader->getShaderStages().size());
        pipelineInfo.pStages = meshShader->getShaderStages().data();
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

        graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        // Create masked pipeline (same as opaque - shader will handle alpha discard)
        maskedPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        // Create translucent pipeline with alpha blending
        vk::PipelineColorBlendAttachmentState translucentBlendAttachment{};
        translucentBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                                    vk::ColorComponentFlagBits::eG |
                                                    vk::ColorComponentFlagBits::eB |
                                                    vk::ColorComponentFlagBits::eA;
        translucentBlendAttachment.blendEnable = VK_TRUE;
        translucentBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        translucentBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        translucentBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        translucentBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        translucentBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        translucentBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo translucentBlending{};
        translucentBlending.logicOpEnable = VK_FALSE;
        translucentBlending.attachmentCount = 1;
        translucentBlending.pAttachments = &translucentBlendAttachment;

        // Translucent: depth test enabled but depth write disabled
        vk::PipelineDepthStencilStateCreateInfo translucentDepthStencil{};
        translucentDepthStencil.depthTestEnable = VK_TRUE;
        translucentDepthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
        translucentDepthStencil.depthCompareOp = vk::CompareOp::eLess;
        translucentDepthStencil.depthBoundsTestEnable = VK_FALSE;
        translucentDepthStencil.stencilTestEnable = VK_FALSE;

        pipelineInfo.pDepthStencilState = &translucentDepthStencil;
        pipelineInfo.pColorBlendState = &translucentBlending;

        translucentPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void StaticMeshPipeline::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 2> attachments = {colorView, depth};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void StaticMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                              const glm::vec3& cameraPos, float time) const
    {
        // Store for AABB wireframe rendering, translucent sorting, and animation
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;
        currentTime = time;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void StaticMeshPipeline::cleanUpForReinit()
    {
        // Clean up pipeline/descriptor resources but preserve loaded meshes and command pool

        for (auto& framebuffer : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(framebuffer);
        }
        framebuffers.clear();

        if (cameraUBO)
        {
            device.getLogicalDevice().destroyBuffer(cameraUBO);
            device.getLogicalDevice().freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
            cameraUBOMemory = nullptr;
        }

        if (renderPass)
            device.getLogicalDevice().destroyRenderPass(renderPass);
        if (graphicsPipeline)
            device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        if (maskedPipeline)
            device.getLogicalDevice().destroyPipeline(maskedPipeline);
        if (translucentPipeline)
            device.getLogicalDevice().destroyPipeline(translucentPipeline);
        if (pipelineLayout)
            device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        if (descriptorPool)
        {
            if (descriptorSet)
                device.getLogicalDevice().freeDescriptorSets(descriptorPool, descriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        }
        if (descriptorSetLayout)
            device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        // Clean up texture descriptor set (set 1)
        if (textureDescriptorPool)
        {
            if (textureDescriptorSet)
                device.getLogicalDevice().freeDescriptorSets(textureDescriptorPool, textureDescriptorSet);
            device.getLogicalDevice().destroyDescriptorPool(textureDescriptorPool);
            textureDescriptorPool = nullptr;
            textureDescriptorSet = nullptr;
        }
        if (textureDescriptorSetLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(textureDescriptorSetLayout);
            textureDescriptorSetLayout = nullptr;
        }
        textureDescriptorsInitialized = false;

        // Clean up wireframe pipeline
        if (wireframePipeline)
        {
            device.getLogicalDevice().destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }
        if (wireframePipelineLayout)
        {
            device.getLogicalDevice().destroyPipelineLayout(wireframePipelineLayout);
            wireframePipelineLayout = nullptr;
        }

        // Clean up AABB buffers
        if (aabbVertexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(aabbVertexBuffer);
            device.getLogicalDevice().freeMemory(aabbVertexBufferMemory);
            aabbVertexBuffer = nullptr;
            aabbVertexBufferMemory = nullptr;
        }
        if (aabbIndexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(aabbIndexBuffer);
            device.getLogicalDevice().freeMemory(aabbIndexBufferMemory);
            aabbIndexBuffer = nullptr;
            aabbIndexBufferMemory = nullptr;
        }

        renderPass = nullptr;
        graphicsPipeline = nullptr;
        pipelineLayout = nullptr;
        descriptorSet = nullptr;
        descriptorPool = nullptr;
        descriptorSetLayout = nullptr;

        // Clean up default textures if we created them
        if (usingDefaultTextures)
        {
            auto cleanupImageData = [&](ibl::ImageData& imageData) {
                if (imageData.sampler) device.getLogicalDevice().destroySampler(imageData.sampler);
                if (imageData.imageView) device.getLogicalDevice().destroyImageView(imageData.imageView);
                if (imageData.image) device.getLogicalDevice().destroyImage(imageData.image);
                if (imageData.imageMemory) device.getLogicalDevice().freeMemory(imageData.imageMemory);
                imageData = {};
            };

            cleanupImageData(defaultIrradiance);
            cleanupImageData(defaultPrefilter);
            cleanupImageData(defaultBrdfLUT);
            usingDefaultTextures = false;
        }
    }

    void StaticMeshPipeline::cleanUp()
    {
        // Clear material cache
        {
            std::unique_lock<std::shared_mutex> lock(materialCacheMutex);
            materialCache.clear();
        }

        // Clean up material shader cache (per-material compiled pipelines)
        if (materialShaderCache) {
            materialShaderCache->cleanUp();
        }

        // Clean up material textures
        if (textureCache) {
            textureCache->cleanUp();
        }

        // Unload all meshes (meshCache will wait for pending transfers)
        unloadAllMeshes();

        // Clean up pipeline/descriptor resources
        cleanUpForReinit();

        // Reset caches (must happen before command pool reset)
        textureCache.reset();
        meshCache.reset();

        // Reset command pool (automatic cleanup via UniqueCommandPool)
        commandPool.reset();
    }

    void StaticMeshPipeline::cleanUpShader()
    {
        meshShader->cleanUp();
        wireframeShader->cleanUp();
    }

    void StaticMeshPipeline::invalidateMaterialCache(const std::string& materialPath)
    {
        std::unique_lock<std::shared_mutex> lock(materialCacheMutex);
        if (materialPath.empty()) {
            // Mark cache for full invalidation
            materialCacheInvalidated = true;
            // Also invalidate all compiled shaders
            if (materialShaderCache) {
                materialShaderCache->invalidateAll();
            }
        } else {
            // Clear specific material immediately
            materialCache.erase(materialPath);
            // Also invalidate the compiled shader for this material
            if (materialShaderCache) {
                materialShaderCache->invalidate(materialPath);
            }
        }
    }

    void StaticMeshPipeline::injectMaterialForPreview(const std::string& materialPath,
                                                       std::shared_ptr<material::MaterialData> materialData)
    {
        if (materialPath.empty() || !materialData) {
            return;
        }

        {
            std::unique_lock<std::shared_mutex> lock(materialCacheMutex);
            // Inject/update the material in the cache
            materialCache[materialPath] = materialData;
        }

        // If the material has custom shaders, invalidate the shader cache to force recompilation
        // This ensures that if the shader code changed, it gets recompiled
        if (!materialData->cachedVertexShader.empty() && !materialData->cachedFragmentShader.empty()) {
            if (materialShaderCache) {
                materialShaderCache->invalidate(materialPath);
            }
        }
    }

    std::string StaticMeshPipeline::getLastShaderCompilationError() const
    {
        if (materialShaderCache) {
            return materialShaderCache->getLastCompilationError();
        }
        return "";
    }

    std::string StaticMeshPipeline::loadMesh(std::string_view meshPath)
    {
        return meshCache->loadMesh(meshPath);
    }

    std::string StaticMeshPipeline::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        return meshCache->uploadMesh(meshId, meshesData);
    }

    void StaticMeshPipeline::unloadMesh(const std::string& meshId)
    {
        meshCache->unloadMesh(meshId);
    }

    void StaticMeshPipeline::unloadAllMeshes()
    {
        meshCache->unloadAllMeshes();
    }

    const MeshGPUData* StaticMeshPipeline::getMesh(const std::string& meshId) const
    {
        return meshCache->getMesh(meshId);
    }

    bool StaticMeshPipeline::isMeshLoaded(const std::string& meshId) const
    {
        return meshCache->isMeshLoaded(meshId);
    }

    const math::AABB* StaticMeshPipeline::getMeshBoundingBox(const std::string& meshId) const
    {
        return meshCache->getMeshBoundingBox(meshId);
    }

    std::vector<std::string> StaticMeshPipeline::getLoadedMeshIds() const
    {
        return meshCache->getLoadedMeshIds();
    }

    void StaticMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                  uint32_t imageIndex,
                                                  const std::vector<MeshRenderData>& meshDrawList,
                                                  const math::Frustum* frustum) const
    {
        if (meshDrawList.empty())
        {
            return;
        }

        // Prepare textures for this frame (load, assign slots, update descriptor set)
        prepareTexturesForFrame(meshDrawList);

        // Acquire shared lock for reading material cache during rendering
        std::shared_lock<std::shared_mutex> cacheLock(materialCacheMutex);

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

        // Clear values for depth only - color uses loadOp::eLoad to preserve skybox
        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Bind descriptor sets (shared across all pipelines)
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        // Bind texture descriptor set (set 1) if available
        if (textureDescriptorsInitialized && textureDescriptorSet)
        {
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                             pipelineLayout, 1, textureDescriptorSet, nullptr);
        }

        // Helper lambda to render a submesh with the correct pipeline
        auto renderSubmesh = [&](const MeshRenderData& meshData, const SubMeshGPUData& subMesh,
                                 size_t subMeshIndex, material::BlendMode targetBlendMode,
                                 vk::Pipeline& currentPipeline) {
            // Get PBR values from assigned material (or fallback to mesh defaults)
            ExtractedPBRValues pbrValues = getPBRForSubmesh(meshData, subMesh.name, materialCache, currentTime);

            // Skip if blend mode doesn't match current pass
            if (pbrValues.blendMode != targetBlendMode) return;

            // Try to get material-specific pipeline from shader cache
            vk::Pipeline targetPipeline = nullptr;
            bool usingMaterialPipeline = false;

            if (materialShaderCache && !pbrValues.materialPath.empty()) {
                // Check if material has custom shaders
                auto matIt = materialCache.find(pbrValues.materialPath);
                if (matIt != materialCache.end() && matIt->second) {
                    const material::MaterialData& matData = *matIt->second;
                    // Only use custom pipeline if material has compiled shaders
                    if (!matData.cachedVertexShader.empty() && !matData.cachedFragmentShader.empty()) {
                        const MaterialPipelineData* matPipeline =
                            materialShaderCache->getOrCreatePipeline(pbrValues.materialPath, matData);
                        if (matPipeline && matPipeline->valid) {
                            // Select appropriate pipeline variant based on blend mode
                            if (pbrValues.blendMode == material::BlendMode::Masked) {
                                targetPipeline = matPipeline->maskedPipeline;
                            } else if (pbrValues.blendMode == material::BlendMode::Translucent) {
                                targetPipeline = matPipeline->translucentPipeline;
                            } else {
                                targetPipeline = matPipeline->opaquePipeline;
                            }
                            usingMaterialPipeline = true;
                        }
                    }
                }
            }

            // Fall back to default pipeline if no custom shader
            if (!usingMaterialPipeline) {
                if (pbrValues.blendMode == material::BlendMode::Masked) {
                    targetPipeline = maskedPipeline;
                } else if (pbrValues.blendMode == material::BlendMode::Translucent) {
                    targetPipeline = translucentPipeline;
                } else {
                    targetPipeline = graphicsPipeline;
                }
            }

            if (currentPipeline != targetPipeline) {
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, targetPipeline);
                currentPipeline = targetPipeline;
            }

            // Setup push constants with transform and material properties
            MeshPushConstants pushConstants{};
            pushConstants.model = meshData.modelMatrix;
            pushConstants.metallic = pbrValues.metallic;
            pushConstants.roughness = pbrValues.roughness;
            pushConstants.ao = pbrValues.ao;
            pushConstants.blendMode = static_cast<float>(pbrValues.blendMode);

            // Get texture slot indices from loaded textures
            auto getTexIdx = [this, &meshData](const std::string& texPath, float fallbackIdx) -> float {
                if (!texPath.empty()) {
                    return static_cast<float>(textureCache->getTextureSlot(texPath));
                }
                return fallbackIdx;
            };

            pushConstants.albedoTexIdx = getTexIdx(pbrValues.albedoTexturePath, meshData.albedoTexIdx);
            pushConstants.metallicTexIdx = getTexIdx(pbrValues.metallicTexturePath, meshData.metallicTexIdx);
            pushConstants.roughnessTexIdx = getTexIdx(pbrValues.roughnessTexturePath, meshData.roughnessTexIdx);
            pushConstants.aoTexIdx = getTexIdx(pbrValues.aoTexturePath, meshData.aoTexIdx);
            pushConstants.normalTexIdx = getTexIdx(pbrValues.normalTexturePath, meshData.normalTexIdx);
            pushConstants.emissionTexIdx = getTexIdx(pbrValues.emissionTexturePath, meshData.emissionTexIdx);

            // Highlight selected submesh with different color and emission
            if (meshData.highlightedSubMesh >= 0 &&
                static_cast<size_t>(meshData.highlightedSubMesh) == subMeshIndex)
            {
                pushConstants.albedo = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
                pushConstants.emission = 0.8f;
            }
            else
            {
                pushConstants.albedo = pbrValues.albedo;
                pushConstants.emission = pbrValues.emission;
            }

            commandBuffer.pushConstants(pipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(MeshPushConstants), &pushConstants);

            // Bind vertex buffer
            vk::Buffer vertexBuffers[] = {subMesh.vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            // Draw with indices if available, otherwise draw vertices directly
            if (subMesh.indexCount > 0)
            {
                commandBuffer.bindIndexBuffer(subMesh.indexBuffer, 0, vk::IndexType::eUint32);
                commandBuffer.drawIndexed(subMesh.indexCount, 1, 0, 0, 0);
            }
            else
            {
                commandBuffer.draw(subMesh.vertexCount, 1, 0, 0);
            }
        };

        vk::Pipeline currentPipeline = nullptr;

        // Pass 1: Render opaque objects
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        currentPipeline = graphicsPipeline;
        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty()) continue;

            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];
                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix)) continue;
                renderSubmesh(meshData, subMesh, subMeshIndex, material::BlendMode::Opaque, currentPipeline);
            }
        }

        // Pass 2: Render masked objects (alpha testing)
        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty()) continue;

            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];
                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix)) continue;
                renderSubmesh(meshData, subMesh, subMeshIndex, material::BlendMode::Masked, currentPipeline);
            }
        }

        // Pass 3: Render translucent objects (alpha blending) - sorted back-to-front
        // Collect translucent submeshes with distance from camera
        struct TranslucentItem {
            const MeshRenderData* meshData;
            const SubMeshGPUData* subMesh;
            size_t subMeshIndex;
            float distanceSquared;
        };
        std::vector<TranslucentItem> translucentItems;

        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty()) continue;

            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];
                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix)) continue;

                // Check if this submesh is translucent
                ExtractedPBRValues pbrValues = getPBRForSubmesh(meshData, subMesh.name, materialCache, currentTime);
                if (pbrValues.blendMode != material::BlendMode::Translucent) continue;

                // Calculate world-space center of submesh AABB
                glm::vec3 localCenter = subMesh.boundingBox.getCenter();
                glm::vec4 worldCenter = meshData.modelMatrix * glm::vec4(localCenter, 1.0f);

                // Calculate squared distance from camera (avoid sqrt for performance)
                glm::vec3 diff = glm::vec3(worldCenter) - currentCameraPos;
                float distSq = glm::dot(diff, diff);

                translucentItems.push_back({&meshData, &subMesh, subMeshIndex, distSq});
            }
        }

        // Sort back-to-front (farthest first)
        std::sort(translucentItems.begin(), translucentItems.end(),
            [](const TranslucentItem& a, const TranslucentItem& b) {
                return a.distanceSquared > b.distanceSquared;
            });

        // Render sorted translucent items
        for (const auto& item : translucentItems)
        {
            renderSubmesh(*item.meshData, *item.subMesh, item.subMeshIndex,
                          material::BlendMode::Translucent, currentPipeline);
        }

        // Render AABB wireframes for meshes with showBoundingBox enabled
        if (wireframePipeline && aabbVertexBuffer)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

            vk::Buffer vertexBuffers[] = {aabbVertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
            commandBuffer.bindIndexBuffer(aabbIndexBuffer, 0, vk::IndexType::eUint32);

            for (const auto& meshData : meshDrawList)
            {
                if (!meshData.showBoundingBox)
                {
                    continue;
                }

                const MeshGPUData* gpuData = getMesh(meshData.meshPath);
                if (!gpuData)
                {
                    continue;
                }

                // Render combined mesh AABB (green)
                {
                    const math::AABB& aabb = gpuData->boundingBox;
                    glm::vec3 center = aabb.getCenter();
                    glm::vec3 extents = aabb.getExtents();

                    // Scale and translate unit cube [-1,1] to AABB bounds
                    glm::mat4 aabbModel = meshData.modelMatrix;
                    aabbModel = glm::translate(aabbModel, center);
                    aabbModel = glm::scale(aabbModel, extents);

                    // Calculate MVP
                    glm::mat4 mvp = currentProjection * currentView * aabbModel;

                    AABBPushConstants aabbPushConstants{};
                    aabbPushConstants.mvp = mvp;
                    aabbPushConstants.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green wireframe

                    commandBuffer.pushConstants(wireframePipelineLayout,
                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                        0, sizeof(AABBPushConstants), &aabbPushConstants);

                    // Draw unit cube wireframe (24 indices for 12 lines)
                    commandBuffer.drawIndexed(24, 1, 0, 0, 0);
                }

                // Render per-submesh AABBs (yellow) - only if there are multiple submeshes
                if (gpuData->subMeshes.size() > 1)
                {
                    for (const auto& subMesh : gpuData->subMeshes)
                    {
                        const math::AABB& aabb = subMesh.boundingBox;
                        glm::vec3 center = aabb.getCenter();
                        glm::vec3 extents = aabb.getExtents();

                        // Scale and translate unit cube [-1,1] to AABB bounds
                        glm::mat4 aabbModel = meshData.modelMatrix;
                        aabbModel = glm::translate(aabbModel, center);
                        aabbModel = glm::scale(aabbModel, extents);

                        // Calculate MVP
                        glm::mat4 mvp = currentProjection * currentView * aabbModel;

                        AABBPushConstants aabbPushConstants{};
                        aabbPushConstants.mvp = mvp;
                        aabbPushConstants.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow wireframe

                        commandBuffer.pushConstants(wireframePipelineLayout,
                            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                            0, sizeof(AABBPushConstants), &aabbPushConstants);

                        // Draw unit cube wireframe (24 indices for 12 lines)
                        commandBuffer.drawIndexed(24, 1, 0, 0, 0);
                    }
                }
            }
        }

        commandBuffer.endRenderPass();
    }

    void StaticMeshPipeline::createWireframePipeline()
    {
        // Push constant range for MVP + color
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(AABBPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 0;  // No descriptor sets needed
        pipelineLayoutInfo.pSetLayouts = nullptr;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        wireframePipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Vertex input - simple vec3 positions
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(glm::vec3);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;

        vk::VertexInputAttributeDescription attributeDescription{};
        attributeDescription.binding = 0;
        attributeDescription.location = 0;
        attributeDescription.format = vk::Format::eR32G32B32Sfloat;
        attributeDescription.offset = 0;

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = 1;
        vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;  // Use Fill - we're drawing lines via LineList topology
        rasterizer.lineWidth = 1.0f;  // Must be 1.0 without wideLines feature
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;  // Don't write depth for wireframe
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

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

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(wireframeShader->getShaderStages().size());
        pipelineInfo.pStages = wireframeShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = wireframePipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create wireframe graphics pipeline");
        }
        wireframePipeline = result.value;
    }

    void StaticMeshPipeline::createAABBBuffers()
    {
        // Unit cube vertices (8 corners, from -1 to 1)
        std::vector<glm::vec3> vertices = {
            {-1.0f, -1.0f, -1.0f},  // 0: back-bottom-left
            { 1.0f, -1.0f, -1.0f},  // 1: back-bottom-right
            { 1.0f,  1.0f, -1.0f},  // 2: back-top-right
            {-1.0f,  1.0f, -1.0f},  // 3: back-top-left
            {-1.0f, -1.0f,  1.0f},  // 4: front-bottom-left
            { 1.0f, -1.0f,  1.0f},  // 5: front-bottom-right
            { 1.0f,  1.0f,  1.0f},  // 6: front-top-right
            {-1.0f,  1.0f,  1.0f},  // 7: front-top-left
        };

        // Line indices for 12 edges of the cube
        std::vector<uint32_t> indices = {
            // Back face edges
            0, 1,  1, 2,  2, 3,  3, 0,
            // Front face edges
            4, 5,  5, 6,  6, 7,  7, 4,
            // Connecting edges
            0, 4,  1, 5,  2, 6,  3, 7
        };

        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexRequest, aabbVertexBuffer, aabbVertexBufferMemory);

        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            commandPool.get(),
            aabbVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(indexRequest, aabbIndexBuffer, aabbIndexBufferMemory);

        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            commandPool.get(),
            aabbIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void StaticMeshPipeline::prepareTexturesForFrame(const std::vector<MeshRenderData>& meshDrawList) const
    {
        // Check if any meshes have materials. If not (e.g., material preview),
        // skip texture preparation to preserve externally-bound textures
        bool hasMaterials = false;
        for (const auto& meshData : meshDrawList) {
            if (!meshData.defaultMaterialPath.empty() || !meshData.submeshMaterials.empty()) {
                hasMaterials = true;
                break;
            }
        }

        if (!hasMaterials) {
            // No materials in draw list - don't reset texture bindings
            // This allows external texture management (e.g., MaterialPreviewController)
            return;
        }

        // Acquire exclusive lock for material cache operations
        std::unique_lock<std::shared_mutex> cacheLock(materialCacheMutex);

        // Check if cache needs to be invalidated (material was saved externally)
        if (materialCacheInvalidated) {
            materialCache.clear();
            materialCacheInvalidated = false;
        }

        // Reset slot assignments for this frame
        textureCache->resetSlotAssignments();

        // Collect and load all unique textures from materials
        for (const auto& meshData : meshDrawList) {
            // Get materials for this mesh
            std::string materialPath = meshData.defaultMaterialPath;

            // Load material and extract texture paths
            if (!materialPath.empty()) {
                auto cacheIt = materialCache.find(materialPath);
                std::shared_ptr<material::MaterialData> matData;

                if (cacheIt != materialCache.end() && cacheIt->second) {
                    matData = cacheIt->second;
                } else {
                    matData = material::MaterialManager::instance().loadMaterial(materialPath);
                    if (matData) {
                        materialCache[materialPath] = matData;
                    }
                }

                if (matData) {
                    ExtractedPBRValues pbr = extractPBRFromMaterial(*matData);

                    // Load textures if paths are specified
                    if (!pbr.albedoTexturePath.empty()) {
                        textureCache->loadTexture(pbr.albedoTexturePath);
                        textureCache->getTextureSlot(pbr.albedoTexturePath);
                    }
                    if (!pbr.metallicTexturePath.empty()) {
                        textureCache->loadTexture(pbr.metallicTexturePath);
                        textureCache->getTextureSlot(pbr.metallicTexturePath);
                    }
                    if (!pbr.roughnessTexturePath.empty()) {
                        textureCache->loadTexture(pbr.roughnessTexturePath);
                        textureCache->getTextureSlot(pbr.roughnessTexturePath);
                    }
                    if (!pbr.aoTexturePath.empty()) {
                        textureCache->loadTexture(pbr.aoTexturePath);
                        textureCache->getTextureSlot(pbr.aoTexturePath);
                    }
                    if (!pbr.normalTexturePath.empty()) {
                        textureCache->loadTexture(pbr.normalTexturePath);
                        textureCache->getTextureSlot(pbr.normalTexturePath);
                    }
                    if (!pbr.emissionTexturePath.empty()) {
                        textureCache->loadTexture(pbr.emissionTexturePath);
                        textureCache->getTextureSlot(pbr.emissionTexturePath);
                    }
                }
            }

            // Also process per-submesh materials
            for (const auto& [submeshName, matInfo] : meshData.submeshMaterials) {
                if (!matInfo.materialPath.empty()) {
                    auto cacheIt = materialCache.find(matInfo.materialPath);
                    std::shared_ptr<material::MaterialData> matData;

                    if (cacheIt != materialCache.end() && cacheIt->second) {
                        matData = cacheIt->second;
                    } else {
                        matData = material::MaterialManager::instance().loadMaterial(matInfo.materialPath);
                        if (matData) {
                            materialCache[matInfo.materialPath] = matData;
                        }
                    }

                    if (matData) {
                        ExtractedPBRValues pbr = extractPBRFromMaterial(*matData);

                        if (!pbr.albedoTexturePath.empty()) {
                            textureCache->loadTexture(pbr.albedoTexturePath);
                            textureCache->getTextureSlot(pbr.albedoTexturePath);
                        }
                        if (!pbr.metallicTexturePath.empty()) {
                            textureCache->loadTexture(pbr.metallicTexturePath);
                            textureCache->getTextureSlot(pbr.metallicTexturePath);
                        }
                        if (!pbr.roughnessTexturePath.empty()) {
                            textureCache->loadTexture(pbr.roughnessTexturePath);
                            textureCache->getTextureSlot(pbr.roughnessTexturePath);
                        }
                        if (!pbr.aoTexturePath.empty()) {
                            textureCache->loadTexture(pbr.aoTexturePath);
                            textureCache->getTextureSlot(pbr.aoTexturePath);
                        }
                        if (!pbr.normalTexturePath.empty()) {
                            textureCache->loadTexture(pbr.normalTexturePath);
                            textureCache->getTextureSlot(pbr.normalTexturePath);
                        }
                        if (!pbr.emissionTexturePath.empty()) {
                            textureCache->loadTexture(pbr.emissionTexturePath);
                            textureCache->getTextureSlot(pbr.emissionTexturePath);
                        }
                    }
                }
            }
        }

        // Update texture descriptor set with bound textures
        const_cast<StaticMeshPipeline*>(this)->updateTextureDescriptors(
            textureCache->getImageViews(),
            textureCache->getSamplers()
        );
    }
}
