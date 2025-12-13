#include "StaticMeshPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Utilities.hpp"
#include "resource/MeshResource.hpp"
#include "material/MaterialManager.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <optional>

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
    };

    // Helper to get value from a node connected to a specific pin
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

        // Try to get Emission
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Emission")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.emission = std::get<float>(*val);
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

        return pbr;
    }

    // Get PBR values for a submesh, checking material assignments in order:
    // 1. Per-submesh material override
    // 2. Default material for the mesh
    // 3. Fallback defaults from MeshRenderData
    static ExtractedPBRValues getPBRForSubmesh(
        const MeshRenderData& meshData,
        const std::string& submeshName,
        std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache)
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
            // Check cache first
            auto cacheIt = matCache.find(materialPath);
            if (cacheIt != matCache.end() && cacheIt->second) {
                pbr = extractPBRFromMaterial(*cacheIt->second);
            } else {
                // Load and cache the material
                auto matData = material::MaterialManager::instance().loadMaterial(materialPath);
                if (matData) {
                    matCache[materialPath] = matData;
                    pbr = extractPBRFromMaterial(*matData);
                }
            }
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

        // Create async transfer manager - uses dedicated transfer queue if available
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    void StaticMeshPipeline::init(const ibl::ImageData& irradianceMap,
                                  const ibl::ImageData& prefilterMap,
                                  const ibl::ImageData& brdfLUT)
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createCameraUBO();
        createDescriptorSet(irradianceMap, prefilterMap, brdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
        createFramebuffers();
        createWireframePipeline();
        createAABBBuffers();
    }

    void StaticMeshPipeline::initWithDefaults()
    {
        loadShaders();
        createRenderPass();
        createDescriptorSetLayout();
        createDescriptorPool();
        createCameraUBO();
        createDefaultIBLTextures();
        createDescriptorSet(defaultIrradiance, defaultPrefilter, defaultBrdfLUT);
        createPipelineLayout();
        createGraphicsPipeline();
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

    void StaticMeshPipeline::createPipelineLayout()
    {
        // Push constant range for MeshPushConstants
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
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

        // Create pipeline
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
                                              const glm::vec3& cameraPos) const
    {
        // Store for AABB wireframe rendering
        currentView = view;
        currentProjection = projection;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;

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
        // Wait for any pending transfers before cleanup
        if (transferManager) {
            transferManager->waitAll();
        }

        // Clear material cache
        materialCache.clear();

        // Unload all meshes first
        unloadAllMeshes();

        // Clean up pipeline/descriptor resources
        cleanUpForReinit();

        // Reset transfer manager (must happen before command pool reset)
        transferManager.reset();

        // Reset command pool (automatic cleanup via UniqueCommandPool)
        commandPool.reset();
    }

    void StaticMeshPipeline::cleanUpShader()
    {
        meshShader->cleanUp();
        wireframeShader->cleanUp();
    }

    std::string StaticMeshPipeline::loadMesh(std::string_view meshPath)
    {
        std::string pathStr(meshPath);

        if (loadedMeshes.contains(pathStr))
        {
            return pathStr;
        }

        auto meshFuture = resource::ResourceManager::loadMeshAsync(meshPath);
        auto meshesDataPtr = meshFuture.get();

        if (!meshesDataPtr || meshesDataPtr->meshes.empty())
        {
            loggerError("Failed to load mesh from: {}", meshPath);
            return "";
        }

        MeshGPUData gpuData{};

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        // Initialize bounding box with first vertex we find
        bool boundingBoxInitialized = false;

        // Upload all submeshes to GPU
        for (const auto& meshData : meshesDataPtr->meshes)
        {
            if (meshData.vertices.empty())
            {
                loggerWarning("Skipping empty submesh in: {}", meshPath);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;  // Store submesh name for material assignment

            // Compute per-submesh bounding box
            bool subMeshBBInitialized = false;
            for (const auto& vertex : meshData.vertices)
            {
                if (!subMeshBBInitialized)
                {
                    subMesh.boundingBox.min = vertex.position;
                    subMesh.boundingBox.max = vertex.position;
                    subMeshBBInitialized = true;
                }
                else
                {
                    subMesh.boundingBox.expand(vertex.position);
                }

                // Also expand the combined mesh bounding box
                if (!boundingBoxInitialized)
                {
                    gpuData.boundingBox.min = vertex.position;
                    gpuData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    gpuData.boundingBox.expand(vertex.position);
                }
            }
            subMesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            subMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());

            // Create vertex buffer (device local for best performance)
            vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * meshData.vertices.size();

            core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexBufferRequest.size = vertexBufferSize;
            vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(vertexBufferRequest, subMesh.vertexBuffer, subMesh.vertexBufferMemory);

            // Copy vertex data to GPU using async transfer (non-blocking)
            transferManager->copyToBufferAsync(
                subMesh.vertexBuffer,
                meshData.vertices.data(),
                vertexBufferSize
            );

            // Create index buffer if indices exist
            if (!meshData.indices.empty())
            {
                vk::DeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();

                core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                indexBufferRequest.size = indexBufferSize;
                indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::Utilities::createBuffer(indexBufferRequest, subMesh.indexBuffer, subMesh.indexBufferMemory);

                // Copy index data to GPU using async transfer (non-blocking)
                transferManager->copyToBufferAsync(
                    subMesh.indexBuffer,
                    meshData.indices.data(),
                    indexBufferSize
                );
            }

            totalVertices += subMesh.vertexCount;
            totalIndices += subMesh.indexCount;
            gpuData.subMeshes.push_back(subMesh);
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Mesh has no valid submeshes: {}", meshPath);
            return "";
        }

        // Wait for all async transfers to complete before the mesh can be used
        // This is still faster than synchronous transfers because:
        // 1. Multiple submeshes are uploaded in parallel
        // 2. Uses dedicated transfer queue (if available) without blocking graphics
        transferManager->waitAll();

        loadedMeshes[pathStr] = std::move(gpuData);
        loggerInfo("Loaded mesh: {} ({} submeshes, {} total vertices, {} total indices)",
                   meshPath, loadedMeshes[pathStr].subMeshes.size(), totalVertices, totalIndices);

        return pathStr;
    }

    std::string StaticMeshPipeline::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        if (loadedMeshes.contains(meshId))
        {
            return meshId;  // Already loaded
        }

        if (meshesData.meshes.empty())
        {
            loggerError("Cannot upload empty mesh data for: {}", meshId);
            return "";
        }

        MeshGPUData gpuData{};

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        // Initialize bounding box with first vertex we find
        bool boundingBoxInitialized = false;

        // Upload all submeshes to GPU
        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.vertices.empty())
            {
                loggerWarning("Skipping empty submesh in procedural mesh: {}", meshId);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute per-submesh bounding box
            bool subMeshBBInitialized = false;
            for (const auto& vertex : meshData.vertices)
            {
                if (!subMeshBBInitialized)
                {
                    subMesh.boundingBox.min = vertex.position;
                    subMesh.boundingBox.max = vertex.position;
                    subMeshBBInitialized = true;
                }
                else
                {
                    subMesh.boundingBox.expand(vertex.position);
                }

                // Also expand the combined mesh bounding box
                if (!boundingBoxInitialized)
                {
                    gpuData.boundingBox.min = vertex.position;
                    gpuData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    gpuData.boundingBox.expand(vertex.position);
                }
            }
            subMesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            subMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());

            // Create vertex buffer (device local for best performance)
            vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * meshData.vertices.size();

            core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexBufferRequest.size = vertexBufferSize;
            vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(vertexBufferRequest, subMesh.vertexBuffer, subMesh.vertexBufferMemory);

            // Copy vertex data to GPU using async transfer
            transferManager->copyToBufferAsync(
                subMesh.vertexBuffer,
                meshData.vertices.data(),
                vertexBufferSize
            );

            // Create index buffer if indices exist
            if (!meshData.indices.empty())
            {
                vk::DeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();

                core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                indexBufferRequest.size = indexBufferSize;
                indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::Utilities::createBuffer(indexBufferRequest, subMesh.indexBuffer, subMesh.indexBufferMemory);

                // Copy index data to GPU using async transfer
                transferManager->copyToBufferAsync(
                    subMesh.indexBuffer,
                    meshData.indices.data(),
                    indexBufferSize
                );
            }

            totalVertices += subMesh.vertexCount;
            totalIndices += subMesh.indexCount;
            gpuData.subMeshes.push_back(subMesh);
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Procedural mesh has no valid submeshes: {}", meshId);
            return "";
        }

        // Wait for all async transfers to complete
        transferManager->waitAll();

        loadedMeshes[meshId] = std::move(gpuData);
        loggerInfo("Uploaded procedural mesh: {} ({} submeshes, {} vertices, {} indices)",
                   meshId, loadedMeshes[meshId].subMeshes.size(), totalVertices, totalIndices);

        return meshId;
    }

    void StaticMeshPipeline::unloadMesh(const std::string& meshId)
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return;
        }

        const auto& gpuData = it->second;

        // Wait for device to finish using the buffers
        device.getLogicalDevice().waitIdle();

        // Destroy all submesh buffers
        for (const auto& subMesh : gpuData.subMeshes)
        {
            if (subMesh.vertexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(subMesh.vertexBuffer);
                device.getLogicalDevice().freeMemory(subMesh.vertexBufferMemory);
            }
            if (subMesh.indexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(subMesh.indexBuffer);
                device.getLogicalDevice().freeMemory(subMesh.indexBufferMemory);
            }
        }

        loadedMeshes.erase(it);
        loggerInfo("Unloaded mesh: {}", meshId);
    }

    void StaticMeshPipeline::unloadAllMeshes()
    {
        if (loadedMeshes.empty())
        {
            return;
        }

        // Wait for device to finish
        device.getLogicalDevice().waitIdle();

        for (auto& [path, gpuData] : loadedMeshes)
        {
            for (const auto& subMesh : gpuData.subMeshes)
            {
                if (subMesh.vertexBuffer)
                {
                    device.getLogicalDevice().destroyBuffer(subMesh.vertexBuffer);
                    device.getLogicalDevice().freeMemory(subMesh.vertexBufferMemory);
                }
                if (subMesh.indexBuffer)
                {
                    device.getLogicalDevice().destroyBuffer(subMesh.indexBuffer);
                    device.getLogicalDevice().freeMemory(subMesh.indexBufferMemory);
                }
            }
        }

        loadedMeshes.clear();
        loggerInfo("Unloaded all meshes");
    }

    const MeshGPUData* StaticMeshPipeline::getMesh(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        if (it != loadedMeshes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool StaticMeshPipeline::isMeshLoaded(const std::string& meshId) const
    {
        return loadedMeshes.contains(meshId);
    }

    const math::AABB* StaticMeshPipeline::getMeshBoundingBox(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return nullptr;
        }
        return &it->second.boundingBox;
    }

    std::vector<std::string> StaticMeshPipeline::getLoadedMeshIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(loadedMeshes.size());
        for (const auto& [path, _] : loadedMeshes)
        {
            ids.push_back(path);
        }
        return ids;
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

        // Bind pipeline and descriptor set once for all meshes
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         pipelineLayout, 0, descriptorSet, nullptr);

        // Render each mesh in the draw list
        for (const auto& meshData : meshDrawList)
        {
            const MeshGPUData* gpuData = getMesh(meshData.meshPath);
            if (!gpuData || gpuData->subMeshes.empty())
            {
                continue;  // Skip meshes that aren't loaded
            }

            // Render all submeshes (with per-submesh frustum culling and highlighting)
            for (size_t subMeshIndex = 0; subMeshIndex < gpuData->subMeshes.size(); ++subMeshIndex)
            {
                const auto& subMesh = gpuData->subMeshes[subMeshIndex];

                // Per-submesh frustum culling (only if frustum is provided and initialized)
                if (frustum && frustum->isInitialized() &&
                    !frustum->intersectsAABB(subMesh.boundingBox, meshData.modelMatrix))
                {
                    continue;  // Submesh is outside frustum, skip rendering
                }

                // Get PBR values from assigned material (or fallback to mesh defaults)
                ExtractedPBRValues pbrValues = getPBRForSubmesh(meshData, subMesh.name, materialCache);

                // Setup push constants with transform and material properties
                MeshPushConstants pushConstants{};
                pushConstants.model = meshData.modelMatrix;
                pushConstants.metallic = pbrValues.metallic;
                pushConstants.roughness = pbrValues.roughness;
                pushConstants.ao = pbrValues.ao;
                pushConstants.padding = 0.0f;

                // Highlight selected submesh with different color and emission
                if (meshData.highlightedSubMesh >= 0 &&
                    static_cast<size_t>(meshData.highlightedSubMesh) == subMeshIndex)
                {
                    // Highlighted submesh: bright orange color with strong emission glow
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
            }
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
}
