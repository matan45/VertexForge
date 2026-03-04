#include "MaterialPreviewController.hpp"
#include "../core/Device.hpp"
#include "../core/BufferUtilities.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "geometry/SphereGenerator.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Log.hpp"
#include <cmath>
#include <optional>

namespace controllers
{
    std::optional<float> MaterialPreviewController::evaluateFloatValue(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        float time)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type)
        {
        case material::NodeType::ConstantScalar: {
            auto it = node->properties.find("value");
            if (it != node->properties.end() && std::holds_alternative<float>(it->second))
                return std::get<float>(it->second);
            return 0.0f;
        }
        case material::NodeType::Time:
            return time;

        case material::NodeType::Sin:
        case material::NodeType::Cos:
        case material::NodeType::Saturate:
        case material::NodeType::Abs:
        case material::NodeType::OneMinus: {
            float v = getInputFloat(graph, nodeId, "Value", time).value_or(0.0f);
            if (node->type == material::NodeType::Sin) return std::sin(v);
            if (node->type == material::NodeType::Cos) return std::cos(v);
            if (node->type == material::NodeType::Saturate) return std::clamp(v, 0.0f, 1.0f);
            if (node->type == material::NodeType::Abs) return std::abs(v);
            return 1.0f - v;
        }
        case material::NodeType::Multiply:
        case material::NodeType::Add:
        case material::NodeType::Subtract: {
            auto a = getInputFloat(graph, nodeId, "A", time);
            auto b = getInputFloat(graph, nodeId, "B", time);
            bool isMul = (node->type == material::NodeType::Multiply);
            float av = a.value_or(isMul ? 1.0f : 0.0f);
            float bv = b.value_or(isMul ? 1.0f : 0.0f);
            if (isMul) return av * bv;
            if (node->type == material::NodeType::Add) return av + bv;
            return av - bv;
        }
        case material::NodeType::Clamp: {
            float v = getInputFloat(graph, nodeId, "Value", time).value_or(0.0f);
            float mn = getInputFloat(graph, nodeId, "Min", time).value_or(0.0f);
            float mx = getInputFloat(graph, nodeId, "Max", time).value_or(1.0f);
            return std::clamp(v, mn, mx);
        }
        default:
            break;
        }

        return std::nullopt;
    }

    std::optional<float> MaterialPreviewController::getInputFloat(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == nodeId && link.targetPin == pinName)
            {
                return evaluateFloatValue(graph, link.sourceNodeId, time);
            }
        }
        return std::nullopt;
    }

    float MaterialPreviewController::evaluateEmissionStrength(const material::ShaderGraph& graph, float time)
    {
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

    MaterialPreviewController::MaterialPreviewController()
        : swapChain{*core::VulkanContext::getSwapChain()}
          , device{*core::VulkanContext::getDevice()}
          , offScreen{std::make_unique<render::OffScreenViewPort>(device, swapChain)}
          , textureManager{std::make_unique<TextureManagerImpl>()}
    {
    }

    MaterialPreviewController::~MaterialPreviewController()
    {
        cleanUp();
    }

    static void uploadStagingToImage(core::Device& device, vk::Buffer stagingBuffer,
                                      vk::Image image, uint32_t width, uint32_t height)
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = device.getQueueFamilyIndices().graphicsAndComputeFamily.value();
        auto commandPool = device.getLogicalDevice().createCommandPoolUnique(poolInfo);

        auto cmdA = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        core::ImageUtilities::transitionImageLayout(cmdA.get(), image, vk::ImageLayout::eUndefined,
                                                    vk::ImageLayout::eTransferDstOptimal,
                                                    vk::ImageAspectFlagBits::eColor);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdA);

        auto cmdCopy = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D(width, height, 1);
        cmdCopy.get().copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, region);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdCopy);

        auto cmdB = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), commandPool.get());
        core::ImageUtilities::transitionImageLayout(cmdB.get(), image, vk::ImageLayout::eTransferDstOptimal,
                                                    vk::ImageLayout::eShaderReadOnlyOptimal,
                                                    vk::ImageAspectFlagBits::eColor);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmdB);
    }

    static void createSamplerAndView(core::Device& device, PreviewTextureGPU& tex,
                                      bool enableAnisotropy)
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        tex.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), tex.image);
        viewRequest.format = vk::Format::eR8G8B8A8Srgb;
        core::ImageUtilities::createImageView(viewRequest, tex.imageView);
    }

    static PreviewTextureGPU uploadPixelsToGPU(core::Device& device, const void* pixels,
                                                uint32_t width, uint32_t height,
                                                bool enableAnisotropy = false)
    {
        PreviewTextureGPU tex{};
        vk::DeviceSize imageSize = width * height * 4;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        core::BufferInfoRequest bufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferInfo.size = imageSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferInfo, stagingBuffer, stagingBufferMemory);

        void* data;
        static_cast<void>(device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &data));
        memcpy(data, pixels, imageSize);
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        core::ImageInfoRequest imageInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageInfo.width = width;
        imageInfo.height = height;
        imageInfo.format = vk::Format::eR8G8B8A8Srgb;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        imageInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::ImageUtilities::createImage(imageInfo, tex.image, tex.memory);

        uploadStagingToImage(device, stagingBuffer, tex.image, width, height);

        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        createSamplerAndView(device, tex, enableAnisotropy);
        tex.valid = true;
        return tex;
    }

    static void createDefaultTextureImpl(core::Device& device, PreviewTextureGPU& defaultTexture)
    {
        std::array<unsigned char, 4> whitePixel = {255, 255, 255, 255};
        defaultTexture = uploadPixelsToGPU(device, whitePixel.data(), 1, 1);
    }

    static PreviewTextureGPU loadTextureFromFileImpl(core::Device& device, const std::string& path)
    {
        if (path.empty())
            return {};

        try
        {
            auto textureData = resource::ResourceManager::loadTextureAsync(path);
            auto texturePtr = textureData.get();
            if (!texturePtr || texturePtr->textureData().empty())
            {
                vfLogWarning("Failed to load texture: {}", path);
                return {};
            }

            auto tex = uploadPixelsToGPU(device, texturePtr->textureData().data(),
                                          texturePtr->width, texturePtr->height, true);
            vfLogInfo("Loaded texture for preview: {}", path);
            return tex;
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception loading texture {}: {}", path, e.what());
            return {};
        }
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
            offScreen = std::make_unique<render::OffScreenViewPort>(device, swapChain);
        }

        offScreen->init();

        // Initialize mesh pipeline with default IBL textures
        // Disable GPU-driven rendering to allow custom per-material shaders
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initMeshPipeline(false);

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
            }
            else
            {
                vfLogError("Failed to create material preview sphere");
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

        if (!initialized)
        {
            return;
        }

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

        std::array<std::string, TextureManagerImpl::MAX_TEXTURES> texturePaths = {
            params.albedoTexturePath, // 0: Albedo
            params.normalTexturePath, // 1: Normal
            params.ormTexturePath, // 2: ORM
            params.metallicTexturePath, // 3: Metallic
            params.roughnessTexturePath, // 4: Roughness
            params.aoTexturePath, // 5: AO
            params.emissionTexturePath, // 6: Emission
            params.heightTexturePath, // 7: Height
            "", "", "", "", "", "", "", "" // 8-15: Reserved
        };

        loadAndAssignTextures(texturePaths);

        bool hasAnyTexture = false;
        for (int i = 0; i < TextureManagerImpl::MAX_TEXTURES && !hasAnyTexture; ++i)
        {
            hasAnyTexture = !textureManager->textureSlots[i].empty();
        }
        bool shouldUpdateBindings = (params.useCustomShader || textureManager->texturesNeedUpdate || hasAnyTexture)
            && textureManager->defaultTexture.valid;

        if (shouldUpdateBindings)
        {
            pendingDescriptorUpdate = true;
        }
    }

    void MaterialPreviewController::loadAndAssignTextures(
        const std::array<std::string, TextureManagerImpl::MAX_TEXTURES>& texturePaths)
    {
        for (int i = 0; i < TextureManagerImpl::MAX_TEXTURES; ++i)
            textureManager->textureSlots[i].clear();

        for (int i = 0; i < TextureManagerImpl::MAX_TEXTURES; ++i)
        {
            const std::string& path = texturePaths[i];
            if (!path.empty())
            {
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
                textureManager->textureSlots[i] = path;
            }
        }
    }

    void MaterialPreviewController::updateTextureDescriptorsIfPending()
    {
        if (!pendingDescriptorUpdate || !textureManager->defaultTexture.valid)
        {
            return;
        }

        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (!meshPipeline)
        {
            return;
        }

        std::array<vk::ImageView, material::MAX_MATERIAL_TEXTURES> imageViews;
        std::array<vk::Sampler, material::MAX_MATERIAL_TEXTURES> samplers;

        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
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
        pendingDescriptorUpdate = false;
    }

    void MaterialPreviewController::cleanUp()
    {
        // Wait for GPU to finish all operations before cleanup
        device.getLogicalDevice().waitIdle();

        if (initialized && sphereLoaded)
        {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                meshPipeline->unloadMesh(SPHERE_MESH_ID);
            }
            sphereLoaded = false;
        }

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

        std::vector<render::mesh::MeshRenderData> meshDrawList;

        render::mesh::MeshRenderData renderData;
        renderData.meshPath = SPHERE_MESH_ID;
        renderData.modelMatrix = glm::mat4(1.0f);
        renderData.albedo = materialParams.albedo;
        renderData.metallic = materialParams.metallic;
        renderData.roughness = materialParams.roughness;
        renderData.ao = materialParams.ao;
        renderData.showBoundingBox = false;
        renderData.highlightedSubMesh = -1;

        renderData.defaultMaterialPath = materialParams.materialPath;

        if (materialParams.materialData)
        {
            renderData.emission = evaluateEmissionStrength(
                materialParams.materialData->graph, currentTime);
        }
        else
        {
            renderData.emission = materialParams.emission;
        }


        renderData.albedoTexIdx() = materialParams.albedoTexturePath.empty() ? -1.0f : 0.0f;
        renderData.normalTexIdx() = materialParams.normalTexturePath.empty() ? -1.0f : 1.0f;
        renderData.ormTexIdx() = materialParams.ormTexturePath.empty() ? -1.0f : 2.0f;
        renderData.metallicTexIdx() = materialParams.metallicTexturePath.empty() ? -1.0f : 3.0f;
        renderData.roughnessTexIdx() = materialParams.roughnessTexturePath.empty() ? -1.0f : 4.0f;
        renderData.aoTexIdx() = materialParams.aoTexturePath.empty() ? -1.0f : 5.0f;
        renderData.emissionTexIdx() = materialParams.emissionTexturePath.empty() ? -1.0f : 6.0f;

        meshDrawList.push_back(renderData);

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);

        vk::DescriptorSet descriptorSet = offScreen->render([this]()
        {
            updateTextureDescriptorsIfPending();
        });
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
