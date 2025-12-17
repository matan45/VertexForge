#include "MaterialPreviewController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/Device.hpp"
#include "../core/Utilities.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "geometry/SphereGenerator.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"
#include <cmath>
#include <optional>

namespace controllers
{
    // Recursively evaluate a float value from the graph (handles Time, Sin, Cos, etc.)
    std::optional<float> MaterialPreviewController::evaluateFloatValue(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        float time)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type)
        {
        case material::NodeType::ConstantScalar:
            {
                auto it = node->properties.find("value");
                if (it != node->properties.end() && std::holds_alternative<float>(it->second))
                {
                    return std::get<float>(it->second);
                }
                return 0.0f;
            }

        case material::NodeType::Time:
            {
                return time;
            }

        case material::NodeType::Sin:
            {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) return std::sin(*inputVal);
                return 0.0f;
            }

        case material::NodeType::Cos:
            {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) return std::cos(*inputVal);
                return 0.0f;
            }

        case material::NodeType::Multiply:
            {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                return (a.value_or(1.0f)) * (b.value_or(1.0f));
            }

        case material::NodeType::Add:
            {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                return (a.value_or(0.0f)) + (b.value_or(0.0f));
            }

        case material::NodeType::Subtract:
            {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                return (a.value_or(0.0f)) - (b.value_or(0.0f));
            }

        case material::NodeType::Clamp:
            {
                auto val = getInputFloat(graph, nodeId, "Value", time);
                auto minVal = getInputFloat(graph, nodeId, "Min", time);
                auto maxVal = getInputFloat(graph, nodeId, "Max", time);
                float v = val.value_or(0.0f);
                float mn = minVal.value_or(0.0f);
                float mx = maxVal.value_or(1.0f);
                return std::clamp(v, mn, mx);
            }

        case material::NodeType::Saturate:
            {
                auto val = getInputFloat(graph, nodeId, "Value", time);
                return std::clamp(val.value_or(0.0f), 0.0f, 1.0f);
            }

        case material::NodeType::Abs:
            {
                auto val = getInputFloat(graph, nodeId, "Value", time);
                return std::abs(val.value_or(0.0f));
            }

        case material::NodeType::OneMinus:
            {
                auto val = getInputFloat(graph, nodeId, "Value", time);
                return 1.0f - val.value_or(0.0f);
            }

        default:
            break;
        }

        return std::nullopt;
    }

    // Get input float from a connected node
    std::optional<float> MaterialPreviewController::getInputFloat(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        // Find link connected to this node's input pin
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == nodeId && link.targetPin == pinName)
            {
                // Recursively evaluate the source node
                return evaluateFloatValue(graph, link.sourceNodeId, time);
            }
        }
        return std::nullopt;
    }

    // Evaluate emission strength from the graph
    float MaterialPreviewController::evaluateEmissionStrength(const material::ShaderGraph& graph, float time)
    {
        // Find PBR Output node
        const auto* outputNode = graph.findOutputNode();
        if (!outputNode)
        {
            return 0.0f;
        }

        // Find what's connected to EmissionStrength pin (not Emission - that's for color)
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == outputNode->id && link.targetPin == "EmissionStrength")
            {
                auto val = evaluateFloatValue(graph, link.sourceNodeId, time);
                if (val)
                {
                    return *val;
                }
            }
        }

        return 0.0f;
    }

    // GPU texture data for preview (internal implementation)
    struct PreviewTextureGPU
    {
        vk::Image image;
        vk::DeviceMemory memory;
        vk::ImageView imageView;
        vk::Sampler sampler;
        bool valid = false;
    };

    // pImpl for texture management
    struct MaterialPreviewController::TextureManagerImpl
    {
        // 6 texture slots per material: albedo, metallic, roughness, ao, normal, emission
        static constexpr int MAX_TEXTURES = 6;
        std::unordered_map<std::string, PreviewTextureGPU> textureCache;
        std::array<std::string, MAX_TEXTURES> textureSlots;
        PreviewTextureGPU defaultTexture;
        bool texturesNeedUpdate = false;

        TextureManagerImpl()
        {
            for (auto& slot : textureSlots)
            {
                slot.clear();
            }
        }
    };

    MaterialPreviewController::MaterialPreviewController()
        : swapChain{*core::VulkanContext::getSwapChain()}
          , device{*core::VulkanContext::getDevice()}
          , offScreen{std::make_unique<imguiPass::OffScreenViewPort>(device, swapChain)}
          , textureManager{std::make_unique<TextureManagerImpl>()}
    {
    }

    MaterialPreviewController::~MaterialPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    static void createDefaultTextureImpl(core::Device& device, PreviewTextureGPU& defaultTexture)
    {
        // Create a 1x1 white texture
        const uint32_t width = 1;
        const uint32_t height = 1;
        const uint32_t imageSize = width * height * 4;
        std::array<unsigned char, 4> whitePixel = {255, 255, 255, 255};

        // Create staging buffer
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        core::BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = imageSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        // Copy data to staging buffer
        void* data;
        static_cast<void>(device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data));
        memcpy(data, whitePixel.data(), imageSize);
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        // Create image
        core::ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = width;
        imageInfo.height = height;
        imageInfo.format = vk::Format::eR8G8B8A8Srgb;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createImage(imageInfo, defaultTexture.image, defaultTexture.memory);

        // Transition and copy
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        auto commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);

        auto cmdA = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        core::Utilities::transitionImageLayout(cmdA.get(), defaultTexture.image, vk::ImageLayout::eUndefined,
                                               vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdA);

        // Copy buffer to image
        auto cmdCopy = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D(width, height, 1);
        cmdCopy.get().copyBufferToImage(stagingBuffer, defaultTexture.image, vk::ImageLayout::eTransferDstOptimal,
                                        region);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdCopy);

        auto cmdB = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        core::Utilities::transitionImageLayout(cmdB.get(), defaultTexture.image, vk::ImageLayout::eTransferDstOptimal,
                                               vk::ImageLayout::eShaderReadOnlyOptimal,
                                               vk::ImageAspectFlagBits::eColor);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdB);

        // Cleanup staging
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        defaultTexture.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Create image view
        core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), defaultTexture.image);
        viewRequest.format = vk::Format::eR8G8B8A8Srgb;
        core::Utilities::createImageView(viewRequest, defaultTexture.imageView);

        defaultTexture.valid = true;
        loggerInfo("Created default white texture for material preview");
    }

    static PreviewTextureGPU loadTextureFromFileImpl(core::Device& device, const std::string& path)
    {
        PreviewTextureGPU tex{};

        if (path.empty())
        {
            return tex;
        }

        try
        {
            auto textureData = resource::ResourceManager::loadTextureAsync(path);
            auto texturePtr = textureData.get();

            if (!texturePtr || texturePtr->textureData.empty())
            {
                loggerWarning("Failed to load texture: {}", path);
                return tex;
            }

            vk::DeviceSize imageSize = texturePtr->width * texturePtr->height * 4 * sizeof(unsigned char);

            // Create staging buffer
            vk::Buffer stagingBuffer;
            vk::DeviceMemory stagingBufferMemory;

            core::BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            bufferInfo.size = imageSize;
            bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
            bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

            // Copy data to staging buffer
            void* data;
            static_cast<void>(device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data));
            memcpy(data, texturePtr->textureData.data(), imageSize);
            device.getLogicalDevice().unmapMemory(stagingBufferMemory);

            // Create image
            core::ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            imageInfo.width = texturePtr->width;
            imageInfo.height = texturePtr->height;
            imageInfo.format = vk::Format::eR8G8B8A8Srgb;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
            imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createImage(imageInfo, tex.image, tex.memory);

            // Create command pool for transfer
            vk::CommandPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
            poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
            auto commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);

            // Transition to transfer dst
            auto cmdA = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
            core::Utilities::transitionImageLayout(cmdA.get(), tex.image, vk::ImageLayout::eUndefined,
                                                   vk::ImageLayout::eTransferDstOptimal,
                                                   vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdA);

            // Copy buffer to image
            auto cmdCopy = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = vk::Extent3D(texturePtr->width, texturePtr->height, 1);
            cmdCopy.get().copyBufferToImage(stagingBuffer, tex.image, vk::ImageLayout::eTransferDstOptimal, region);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdCopy);

            // Transition to shader read
            auto cmdB = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
            core::Utilities::transitionImageLayout(cmdB.get(), tex.image, vk::ImageLayout::eTransferDstOptimal,
                                                   vk::ImageLayout::eShaderReadOnlyOptimal,
                                                   vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdB);

            // Cleanup staging
            device.getLogicalDevice().destroyBuffer(stagingBuffer);
            device.getLogicalDevice().freeMemory(stagingBufferMemory);

            // Create sampler
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eLinear;
            samplerInfo.minFilter = vk::Filter::eLinear;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
            tex.sampler = device.getLogicalDevice().createSampler(samplerInfo);

            // Create image view
            core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), tex.image);
            viewRequest.format = vk::Format::eR8G8B8A8Srgb;
            core::Utilities::createImageView(viewRequest, tex.imageView);

            tex.valid = true;
            loggerInfo("Loaded texture for preview: {}", path);
        }
        catch (const std::exception& e)
        {
            loggerError("Exception loading texture {}: {}", path, e.what());
        }

        return tex;
    }

    static void destroyTextureImpl(core::Device& device, PreviewTextureGPU& tex)
    {
        if (!tex.valid) return;

        device.getLogicalDevice().waitIdle();

        if (tex.imageView)
            device.getLogicalDevice().destroyImageView(tex.imageView);
        if (tex.sampler)
            device.getLogicalDevice().destroySampler(tex.sampler);
        if (tex.image)
            device.getLogicalDevice().destroyImage(tex.image);
        if (tex.memory)
            device.getLogicalDevice().freeMemory(tex.memory);

        tex = PreviewTextureGPU{};
    }

    void MaterialPreviewController::init()
    {
        if (initialized)
        {
            return;
        }

        // Recreate offScreen if it was cleaned up (supports re-initialization)
        if (!offScreen)
        {
            offScreen = std::make_unique<imguiPass::OffScreenViewPort>(device, swapChain);
        }

        offScreen->init();

        // Initialize mesh pipeline with default IBL textures
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initMeshPipeline();

        // Generate and upload procedural sphere
        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (meshPipeline)
        {
            geometry::SphereParams sphereParams;
            sphereParams.radius = 1.0f;
            sphereParams.latitudeSegments = 32;
            sphereParams.longitudeSegments = 32;

            resource::MeshesData sphereData = geometry::SphereGenerator::generateMeshesData(sphereParams);
            std::string result = meshPipeline->uploadMesh(SPHERE_MESH_ID, sphereData);
            sphereLoaded = !result.empty();

            if (sphereLoaded)
            {
                loggerInfo("Material preview sphere created");
            }
            else
            {
                loggerError("Failed to create material preview sphere");
            }
        }

        // Create default white texture for empty slots
        createDefaultTextureImpl(device, textureManager->defaultTexture);

        initialized = true;
    }

    int MaterialPreviewController::getTextureSlot(const std::string& path) const
    {
        if (path.empty()) return -1;

        for (int i = 0; i < TextureManagerImpl::MAX_TEXTURES; ++i)
        {
            if (textureManager->textureSlots[i] == path)
            {
                return i;
            }
        }
        return -1;
    }

    void MaterialPreviewController::setMaterialParams(const PreviewMaterialParams& params)
    {
        materialParams = params;

        if (!initialized) return;

        // Inject material into mesh pipeline cache for custom shader support
        // Only inject when useCustomShader is true (after explicit compile)
        if (params.useCustomShader && !params.materialPath.empty() && params.materialData)
        {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                meshPipeline->injectMaterialForPreview(params.materialPath, params.materialData);
            }
        }
        
        std::array<std::string, 6> texturePaths = {
            params.albedoTexturePath,
            params.metallicTexturePath,
            params.roughnessTexturePath,
            params.aoTexturePath,
            params.normalTexturePath,
            params.emissionTexturePath
        };

        // Clear previous slot assignments and assign textures to fixed slots
        for (int i = 0; i < 6; ++i)
        {
            textureManager->textureSlots[i].clear();
        }

        for (int i = 0; i < 6; ++i)
        {
            const std::string& path = texturePaths[i];
            if (!path.empty())
            {
                // Load texture if not already in cache
                if (textureManager->textureCache.find(path) == textureManager->textureCache.end())
                {
                    auto tex = loadTextureFromFileImpl(device, path);
                    if (tex.valid)
                    {
                        textureManager->textureCache[path] = std::move(tex);
                        textureManager->texturesNeedUpdate = true;
                    }
                    else
                    {
                        vfLogWarning("Failed to load texture for slot {}: {}", i, path);
                    }
                }
                // Always assign to fixed slot regardless of cache status
                textureManager->textureSlots[i] = path;
            }
        }

        // Update bindings if needed
        if (textureManager->texturesNeedUpdate && textureManager->defaultTexture.valid)
        {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                // Build arrays of image views and samplers (6 per material)
                std::array<vk::ImageView, 6> imageViews;
                std::array<vk::Sampler, 6> samplers;

                for (int i = 0; i < 6; ++i)
                {
                    if (!textureManager->textureSlots[i].empty())
                    {
                        auto it = textureManager->textureCache.find(textureManager->textureSlots[i]);
                        if (it != textureManager->textureCache.end() && it->second.valid)
                        {
                            imageViews[i] = it->second.imageView;
                            samplers[i] = it->second.sampler;
                            continue;
                        }
                    }
                    // Use default texture for empty/invalid slots
                    imageViews[i] = textureManager->defaultTexture.imageView;
                    samplers[i] = textureManager->defaultTexture.sampler;
                }

                meshPipeline->updatePreviewTextureDescriptors(imageViews, samplers);
                textureManager->texturesNeedUpdate = false;
            }
        }
    }

    void MaterialPreviewController::cleanUp()
    {
        if (initialized && sphereLoaded)
        {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                meshPipeline->unloadMesh(SPHERE_MESH_ID);
            }
            sphereLoaded = false;
        }

        // Cleanup textures
        if (textureManager)
        {
            for (auto& [path, tex] : textureManager->textureCache)
            {
                destroyTextureImpl(device, tex);
            }
            textureManager->textureCache.clear();

            for (auto& slot : textureManager->textureSlots)
            {
                slot.clear();
            }

            destroyTextureImpl(device, textureManager->defaultTexture);
        }

        if (initialized && offScreen)
        {
            offScreen->cleanUp();
        }

        // Reset offScreen - will be recreated in init() if needed
        offScreen.reset();
        initialized = false;
    }

    void MaterialPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                                 const glm::vec3& cameraPos, float time)
    {
        if (!initialized || !offScreen)
        {
            return;
        }

        // Store time for dynamic graph evaluation
        currentTime = time;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
        {
            return;
        }

        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->getMeshPipeline()->updateCameraUBO(view, projection, cameraPos, time);
        }

        // Update frustum for culling
        currentFrustum.extractFromMatrix(projection * view);
    }

    void* MaterialPreviewController::render()
    {
        if (!initialized || !sphereLoaded)
        {
            return nullptr;
        }

        auto* renderHandler = offScreen->getRenderPassHandler();

        // Create draw list with preview sphere
        std::vector<render::mesh::MeshRenderData> meshDrawList;

        render::mesh::MeshRenderData renderData;
        renderData.meshPath = SPHERE_MESH_ID;
        renderData.modelMatrix = glm::mat4(1.0f); // Sphere at origin
        renderData.albedo = materialParams.albedo;
        renderData.metallic = materialParams.metallic;
        renderData.roughness = materialParams.roughness;
        renderData.ao = materialParams.ao;
        renderData.showBoundingBox = false;
        renderData.highlightedSubMesh = -1;

        // Set material path for custom shader pipeline lookup
        renderData.defaultMaterialPath = materialParams.materialPath;

        // Dynamically evaluate emission from material graph if available
        if (materialParams.materialData)
        {
            renderData.emission = evaluateEmissionStrength(
                materialParams.materialData->graph, currentTime);
        }
        else
        {
            renderData.emission = materialParams.emission;
        }

        
        renderData.albedoTexIdx = materialParams.albedoTexturePath.empty() ? -1.0f : 0.0f;
        renderData.metallicTexIdx = materialParams.metallicTexturePath.empty() ? -1.0f : 1.0f;
        renderData.roughnessTexIdx = materialParams.roughnessTexturePath.empty() ? -1.0f : 2.0f;
        renderData.aoTexIdx = materialParams.aoTexturePath.empty() ? -1.0f : 3.0f;
        renderData.normalTexIdx = materialParams.normalTexturePath.empty() ? -1.0f : 4.0f;
        renderData.emissionTexIdx = materialParams.emissionTexturePath.empty() ? -1.0f : 5.0f;

        meshDrawList.push_back(renderData);

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);

        // Render and return descriptor set
        vk::DescriptorSet descriptorSet = offScreen->render();
        return static_cast<void*>(descriptorSet);
    }

    std::string MaterialPreviewController::getLastShaderCompilationError() const
    {
        if (!initialized || !offScreen) return "";

        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (meshPipeline)
        {
            return meshPipeline->getLastShaderCompilationError();
        }
        return "";
    }
}
