#include "PrefabRigPreviewController.hpp"
#include "../../core/VulkanContext.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/CommandPool.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/RenderManager.hpp"
#include "../../render/mesh/SkinnedMeshPipeline.hpp"
#include "../../render/mesh/SkinnedMeshTypes.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "../../render/ClearColor.hpp"
#include "../../render/preview/PreviewBackgroundRenderer.hpp"
#include "../../render/preview/PreviewGridRenderer.hpp"
#include "print/Log.hpp"
#include <imgui_impl_vulkan.h>

namespace controllers
{
    PrefabRigPreviewController::PrefabRigPreviewController()
        : device{*core::VulkanContext::getDevice()}
          , swapChain{*core::VulkanContext::getSwapChain()}
          , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
    }

    PrefabRigPreviewController::~PrefabRigPreviewController()
    {
        device.getLogicalDevice().waitIdle();
        cleanUp();
    }

    void PrefabRigPreviewController::init()
    {
        if (initialized) return;

        try
        {
            createSampler();
            createOffscreenResources();

            vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
            inFlightFences.resize(swapChain.getImageCount());
            for (auto& fence : inFlightFences)
            {
                fence = device.getLogicalDevice().createFence(fenceInfo);
            }

            clearColor = std::make_unique<render::ClearColor>(device, swapChain, *offscreenResources);
            clearColor->init();

            previewBackground = std::make_unique<render::preview::PreviewBackgroundRenderer>(
                device, swapChain, *offscreenResources);
            previewBackground->init();

            previewGrid = std::make_unique<render::preview::PreviewGridRenderer>(
                device, swapChain, *offscreenResources);
            previewGrid->init();

            initialized = true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to initialize PrefabRigPreviewController: {}", e.what());
            cleanUp();
        }
    }

    void PrefabRigPreviewController::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        // Pipelines hold Vulkan resources; the assembly is entt-free CPU state with no GPU
        // handles, so we just drop the pipelines here. The assembly tears itself down in this
        // controller's destructor (or is rebuilt in place by the next buildFromDesc).
        destroyPipelines();
        built = false;

        if (previewGrid)
        {
            previewGrid->cleanUpShader();
            previewGrid->cleanUp();
            previewGrid.reset();
        }

        if (previewBackground)
        {
            previewBackground->cleanUpShader();
            previewBackground->cleanUp();
            previewBackground.reset();
        }

        if (clearColor)
        {
            clearColor->cleanUp();
            clearColor.reset();
        }

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        if (offscreenResources)
        {
            for (auto const& resources : offscreenResources->colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
        }

        if (sampler)
        {
            device.getLogicalDevice().destroySampler(sampler);
            sampler = nullptr;
        }

        cleanupOffscreenResources();

        if (commandPool)
        {
            commandPool->cleanUp();
        }

        initialized = false;
    }

    void PrefabRigPreviewController::barrierBetweenParts(const vk::CommandBuffer& commandBuffer,
                                                         uint32_t imageIndex) const
    {
        std::array<vk::ImageMemoryBarrier2, 2> barriers{};

        // Color: previous part's writes -> next part's load (read) + write. Stays COLOR_ATTACHMENT_OPTIMAL.
        barriers[0].srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barriers[0].srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        barriers[0].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barriers[0].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite |
                                    vk::AccessFlagBits2::eColorAttachmentRead;
        barriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barriers[0].image = offscreenResources->colorImages[imageIndex].colorImage;
        barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

        // Depth: previous part's depth writes -> next part's depth test (read) + write. Stays
        // DEPTH_STENCIL_ATTACHMENT_OPTIMAL. Covers both early- and late-fragment-test stages.
        barriers[1].srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits2::eLateFragmentTests;
        barriers[1].srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        barriers[1].dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits2::eLateFragmentTests;
        barriers[1].dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                                    vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        barriers[1].oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barriers[1].newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        barriers[1].image = offscreenResources->depthImage.depthImage;
        barriers[1].subresourceRange = {
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1};

        vk::DependencyInfo depInfo{};
        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        depInfo.pImageMemoryBarriers = barriers.data();
        commandBuffer.pipelineBarrier2KHR(depInfo);
    }

    void PrefabRigPreviewController::destroyPipelines()
    {
        for (auto& pipeline : pipelines)
        {
            if (pipeline)
            {
                pipeline->cleanUp();
                pipeline.reset();
            }
        }
        pipelines.clear();
        partMaterial.clear();
    }

    void PrefabRigPreviewController::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        vk::PhysicalDeviceProperties properties = device.getPhysicalDevice().getProperties();
        float maxAnisotropy = properties.limits.maxSamplerAnisotropy;

        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = maxAnisotropy;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void PrefabRigPreviewController::createOffscreenResources()
    {
        offscreenResources = std::make_unique<core::OffscreenResources>();

        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = swapChain.getSwapchainExtent().width;
        imageColorInfo.height = swapChain.getSwapchainExtent().height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = swapChain.getSwapchainExtent().width;
        imageDepthInfo.height = swapChain.getSwapchainExtent().height;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::DepthImage depth;
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);
        imageDepthRequest.format = depthFormat;
        imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(imageDepthRequest, depth.depthImageView);

        vk::UniqueCommandBuffer transitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(transitionDepthImage.get(), depth.depthImage,
                                                    vk::ImageLayout::eUndefined,
                                                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                    vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device, transitionDepthImage);

        offscreenResources->depthImage = std::move(depth);

        offscreenResources->colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageAllocation,
                                              device.getMemoryManager());

            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer transitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionColorImage.get(), color.colorImage,
                                                        vk::ImageLayout::eUndefined,
                                                        vk::ImageLayout::eShaderReadOnlyOptimal,
                                                        vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device, transitionColorImage);

            updateDescriptorSet(color.descriptorSet, color.colorImageView);

            offscreenResources->colorImages.push_back(std::move(color));
        }
    }

    void PrefabRigPreviewController::cleanupOffscreenResources()
    {
        if (!offscreenResources) return;

        for (auto const& resources : offscreenResources->colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
        }
        offscreenResources->colorImages.clear();

        if (offscreenResources->depthImage.depthImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources->depthImage.depthImageView);
        }
        if (offscreenResources->depthImage.depthImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources->depthImage.depthImage);
        }
        if (offscreenResources->depthImage.depthImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources->depthImage.depthImageAllocation);
            offscreenResources->depthImage.depthImageAllocation = {};
        }

        offscreenResources.reset();
    }

    void PrefabRigPreviewController::updateDescriptorSet(vk::DescriptorSet& descriptorSet,
                                                         const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    bool PrefabRigPreviewController::buildFromDesc(const PrefabRigDesc& desc)
    {
        if (!initialized) init();

        // Wait for any in-flight submits before tearing down the previous build's pipelines.
        device.getLogicalDevice().waitIdle();
        destroyPipelines();
        built = false;

        if (!assembly.build(desc))
        {
            vfLogError("PrefabRigPreviewController: assembly build produced no parts");
            return false;
        }

        const size_t partCountN = assembly.partCount();
        pipelines.resize(partCountN);
        partMaterial.assign(partCountN, render::mesh::SkinnedMeshRenderData{}); // neutral defaults

        bool anyRenderable = false;
        for (size_t i = 0; i < partCountN && i < desc.parts.size(); ++i)
        {
            if (buildPartPipeline(i, desc.parts[i]))
            {
                anyRenderable = true;
            }
        }

        if (!anyRenderable)
        {
            vfLogWarning("PrefabRigPreviewController: no part produced a renderable pipeline");
        }

        built = true;
        return built;
    }

    bool PrefabRigPreviewController::buildPartPipeline(size_t partIndex, const PrefabRigPart& descPart)
    {
        auto pipeline = std::make_unique<render::mesh::SkinnedMeshPipeline>(
            device, swapChain, *offscreenResources);
        pipeline->init();

        if (!pipeline->loadMeshFromFile(assembly.meshPath(partIndex)))
        {
            vfLogWarning("PrefabRigPreviewController: part {} failed to load mesh '{}'",
                         partIndex, assembly.meshPath(partIndex));
            pipeline->cleanUp();
            return false; // leave pipelines[partIndex] null => skipped at render time
        }

        // Resolve a material for this part. The window populates defaultMaterialPath (and
        // optional per-submesh overrides) from the prefab's MaterialComponent. We bind a
        // single material per part (the default / first submesh override): the skinned
        // pipeline pushes one material per draw, so per-submesh divergence within a part is
        // out of scope here (the assembly is one .vfMesh per part).
        std::string materialPath = descPart.defaultMaterialPath;
        if (materialPath.empty() && !descPart.subMeshMaterials.empty())
        {
            materialPath = descPart.subMeshMaterials.begin()->second;
        }

        if (!materialPath.empty())
        {
            render::mesh::ExtractedPBRValues pbr =
                render::mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath);

            // Bind the real textures (rewrites the pipeline's set 1) ...
            pipeline->loadMaterial(pbr);

            // ... and cache the scalar PBR + packed texture indices in this part's render data.
            // The shader uses the scalars for any empty texture slot, so a scalar-only material
            // (no albedo/MRA textures) still shows its true color/roughness/etc.
            render::mesh::SkinnedMeshRenderData& mat = partMaterial[partIndex];
            mat.albedo = pbr.albedo;
            mat.metallic = pbr.metallic;
            mat.roughness = pbr.roughness;
            mat.ao = pbr.ao;
            mat.emission = pbr.emission;
            mat.textureIndicesPacked = pipeline->getMaterialTextureIndices();
        }

        pipelines[partIndex] = std::move(pipeline);
        return true;
    }

    void PrefabRigPreviewController::update(float deltaTime)
    {
        if (!built) return;

        assembly.update(deltaTime);

        // Push each renderable part's composed (post-IK) bone matrices to its pipeline. Static
        // parts get the assembly's identity bone set, which the skinned shader treats as a
        // pass-through (zeroed bone weights on a no-skin mesh).
        for (size_t i = 0; i < pipelines.size(); ++i)
        {
            if (!pipelines[i]) continue;
            pipelines[i]->updateBoneMatrices(assembly.boneMatrices(i));
        }
    }

    void PrefabRigPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                                  const glm::vec3& cameraPos)
    {
        currentView = view;
        currentProjection = projection;
        currentCameraPos = cameraPos;

        for (auto& pipeline : pipelines)
        {
            if (pipeline)
            {
                pipeline->updateCameraUBO(view, projection, cameraPos);
            }
        }
    }

    void PrefabRigPreviewController::setRootModelMatrix(const glm::mat4& m)
    {
        assembly.setRootModelMatrix(m);
    }

    bool PrefabRigPreviewController::forceState(size_t part, const std::string& stateName, float blendDuration)
    {
        return assembly.forceState(part, stateName, blendDuration);
    }

    const animator::AnimatorData* PrefabRigPreviewController::animatorData(size_t part) const
    {
        return assembly.animatorData(part);
    }

    void PrefabRigPreviewController::setBool(size_t part, const std::string& name, bool value)
    {
        assembly.setBool(part, name, value);
    }

    void PrefabRigPreviewController::setFloat(size_t part, const std::string& name, float value)
    {
        assembly.setFloat(part, name, value);
    }

    void PrefabRigPreviewController::setInt(size_t part, const std::string& name, int32_t value)
    {
        assembly.setInt(part, name, value);
    }

    void PrefabRigPreviewController::setTrigger(size_t part, const std::string& name)
    {
        assembly.setTrigger(part, name);
    }

    void PrefabRigPreviewController::play() { assembly.play(); }
    void PrefabRigPreviewController::pause() { assembly.pause(); }
    bool PrefabRigPreviewController::isPaused() const { return assembly.isPaused(); }

    std::vector<animator::SocketDefinition>& PrefabRigPreviewController::editableSockets(size_t part)
    {
        return assembly.editableSockets(part);
    }

    const std::vector<animator::SocketDefinition>& PrefabRigPreviewController::editableSockets(size_t part) const
    {
        return assembly.editableSockets(part);
    }

    std::vector<animator::ik::IKChainConfig>& PrefabRigPreviewController::editableChains()
    {
        return assembly.editableChains();
    }

    const std::vector<animator::ik::IKChainConfig>& PrefabRigPreviewController::editableChains() const
    {
        return assembly.editableChains();
    }

    size_t PrefabRigPreviewController::partCount() const { return assembly.partCount(); }

    void* PrefabRigPreviewController::render()
    {
        if (!initialized || !built)
        {
            return nullptr;
        }

        uint32_t imageIndex = core::RenderManager::getImageIndex();

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        // Step 1: clear color + depth ONCE for the whole frame.
        const glm::vec4 clearVal = (environmentParams.backgroundMode == 1)
                                       ? environmentParams.gradientBottomColor
                                       : environmentParams.backgroundColor;
        clearColor->setClearColor(clearVal);
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);

        // Step 2: gradient overlay (gradient mode only).
        if (environmentParams.backgroundMode == 1 && previewBackground && previewBackground->isInitialized())
        {
            previewBackground->render(commandBuffer, imageIndex,
                                      environmentParams.gradientTopColor,
                                      environmentParams.gradientBottomColor);
        }

        // Step 3: each renderable part, loading existing color+depth (depth is NEVER re-cleared
        // between parts — every part composites into the one cleared target).
        bool anyDrawn = false;
        for (size_t i = 0; i < pipelines.size(); ++i)
        {
            if (!pipelines[i]) continue;

            if (!anyDrawn)
            {
                // First part: transition color SHADER_READ_ONLY -> COLOR_ATTACHMENT once.
                core::ImageUtilities::transitionImageLayout(commandBuffer,
                    offscreenResources->colorImages[imageIndex].colorImage,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageAspectFlagBits::eColor);
                anyDrawn = true;
            }
            else
            {
                // Order the previous part's color+depth writes before this part's load.
                barrierBetweenParts(commandBuffer, imageIndex);
            }

            // Start from this part's cached material (scalars + texture indices), then update
            // only the per-frame model matrix from the assembly's resolved part world.
            render::mesh::SkinnedMeshRenderData renderData =
                (i < partMaterial.size()) ? partMaterial[i] : render::mesh::SkinnedMeshRenderData{};
            renderData.modelMatrix = assembly.partWorld(i);

            pipelines[i]->recordCommandBuffer(commandBuffer, imageIndex, renderData,
                                              /*clearAttachments=*/false);
        }

        if (anyDrawn)
        {
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources->colorImages[imageIndex].colorImage,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        // Step 4: grid overlay (handles its own transitions; ends in ShaderReadOnlyOptimal).
        if (previewGrid && previewGrid->isInitialized() && environmentParams.showGrid)
        {
            previewGrid->render(commandBuffer, imageIndex, currentView, currentProjection, true);
        }

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.submitGraphics(submitInfo, inFlightFences[imageIndex]);

        return static_cast<void*>(offscreenResources->colorImages[imageIndex].descriptorSet);
    }
}
