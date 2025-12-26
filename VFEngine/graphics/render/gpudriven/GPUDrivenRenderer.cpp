#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshGPUCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "resource/ResourceManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <array>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven {

    GPUDrivenRenderer::GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    GPUDrivenRenderer::~GPUDrivenRenderer()
    {
        cleanup();
    }

    void GPUDrivenRenderer::init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass)
    {
        if (initialized) {
            return;
        }

        spdlog::info("GPUDrivenRenderer: Initializing...");

        cachedIBLLayout = iblDescriptorSetLayout;
        cachedRenderPass = renderPass;

        // Initialize sub-components
        mergedBuffer = std::make_unique<MergedMeshBuffer>(device);
        mergedBuffer->init();

        indirectBuffer = std::make_unique<IndirectDrawBuffer>(device);
        indirectBuffer->init();

        bindlessTextures = std::make_unique<BindlessTextureManager>(device);
        bindlessTextures->init();

        cullPipeline = std::make_unique<GPUCullLODPipeline>(device);
        cullPipeline->init();

        // Create camera UBO for compute shader
        createCameraBuffer();

        // Create per-draw data descriptor set
        createPerDrawDataDescriptor();

        // Create graphics pipeline for GPU-driven rendering
        createGraphicsPipeline(iblDescriptorSetLayout, renderPass);

        initialized = true;
        spdlog::info("GPUDrivenRenderer: Initialized successfully");
    }

    void GPUDrivenRenderer::cleanup()
    {
        if (!initialized) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Cleanup graphics pipeline
        if (graphicsPipeline) {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (graphicsPipelineLayout) {
            vkDevice.destroyPipelineLayout(graphicsPipelineLayout);
            graphicsPipelineLayout = nullptr;
        }

        if (perDrawDataPool) {
            vkDevice.destroyDescriptorPool(perDrawDataPool);
            perDrawDataPool = nullptr;
        }

        if (perDrawDataLayout) {
            vkDevice.destroyDescriptorSetLayout(perDrawDataLayout);
            perDrawDataLayout = nullptr;
        }

        // Cleanup camera buffer
        if (cameraMapped) {
            vkDevice.unmapMemory(cameraBufferMemory);
            cameraMapped = nullptr;
        }
        if (cameraBuffer) {
            vkDevice.destroyBuffer(cameraBuffer);
            vkDevice.freeMemory(cameraBufferMemory);
            cameraBuffer = nullptr;
        }

        // Cleanup shader
        if (meshShader) {
            meshShader->cleanUp();
            meshShader.reset();
        }

        // Cleanup sub-components
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (indirectBuffer) indirectBuffer->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        cullPipeline.reset();
        bindlessTextures.reset();
        indirectBuffer.reset();
        mergedBuffer.reset();

        initialized = false;
        spdlog::info("GPUDrivenRenderer: Cleaned up");
    }

    void GPUDrivenRenderer::createCameraBuffer()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::BufferInfoRequest request(
            vkDevice,
            device.getPhysicalDevice(),
            sizeof(GPUCameraData),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        core::Utilities::createBuffer(request, cameraBuffer, cameraBufferMemory);

        // Map persistently
        cameraMapped = vkDevice.mapMemory(cameraBufferMemory, 0, sizeof(GPUCameraData));

        spdlog::debug("GPUDrivenRenderer: Created camera buffer");
    }

    void GPUDrivenRenderer::createPerDrawDataDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Create descriptor set layout for per-draw data (Set 1)
        vk::DescriptorSetLayoutBinding perDrawBinding{};
        perDrawBinding.binding = 0;
        perDrawBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawBinding.descriptorCount = 1;
        perDrawBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &perDrawBinding;

        perDrawDataLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        perDrawDataPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = perDrawDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &perDrawDataLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        perDrawDataDescriptorSet = sets[0];

        spdlog::debug("GPUDrivenRenderer: Created per-draw data descriptor");
    }

    void GPUDrivenRenderer::createGraphicsPipeline(vk::DescriptorSetLayout iblLayout, vk::RenderPass renderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Load GPU-driven mesh shader
        meshShader = std::make_unique<core::Shader>(device);
        meshShader->readShader("../../resources/shaders/gpudriven/mesh_gpudriven.glsl");

        const auto& stages = meshShader->getShaderStages();
        if (stages.empty()) {
            spdlog::error("GPUDrivenRenderer: Failed to load mesh shader: {}", meshShader->getLastCompilationError());
            return;
        }

        // Create pipeline layout with 3 descriptor sets:
        // Set 0: IBL (camera UBO + irradiance + prefilter + brdfLUT) - reuse existing layout
        // Set 1: Per-draw data storage buffer
        // Set 2: Bindless textures
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            iblLayout,
            perDrawDataLayout,
            bindlessTextures->getDescriptorSetLayout()
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;

        graphicsPipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // Vertex input state
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

        // Viewport and scissor (dynamic state would be better, but matching existing pattern)
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

        // Color blending - no blending (opaque only for GPU-driven)
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
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = graphicsPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess) {
            spdlog::error("GPUDrivenRenderer: Failed to create graphics pipeline");
            return;
        }

        graphicsPipeline = result.value;
        spdlog::debug("GPUDrivenRenderer: Created graphics pipeline");
    }

    void GPUDrivenRenderer::rebuildMergedBuffer(const mesh::MeshGPUCache& cache)
    {
        if (!initialized || !mergedBuffer) {
            return;
        }

        mergedBuffer->rebuildFromCache(cache);
        spdlog::info("GPUDrivenRenderer: Rebuilt merged buffer with {} vertices, {} indices",
            mergedBuffer->getTotalVertexCount(), mergedBuffer->getTotalIndexCount());
    }

    uint32_t GPUDrivenRenderer::registerTexture(const std::string& path, vk::ImageView view, vk::Sampler sampler)
    {
        if (!initialized || !bindlessTextures) {
            return INVALID_TEXTURE_INDEX;
        }

        return bindlessTextures->registerTexture(path, view, sampler);
    }

    void GPUDrivenRenderer::setDefaultTexture(vk::ImageView view, vk::Sampler sampler)
    {
        if (!initialized || !bindlessTextures) {
            return;
        }

        bindlessTextures->setDefaultTexture(view, sampler);
    }

    vk::DescriptorSetLayout GPUDrivenRenderer::getBindlessTextureLayout() const
    {
        if (bindlessTextures) {
            return bindlessTextures->getDescriptorSetLayout();
        }
        return nullptr;
    }

    void GPUDrivenRenderer::extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6])
    {
        // Extract frustum planes from view-projection matrix (Gribb/Hartmann method)
        // Each plane is represented as (A, B, C, D) where Ax + By + Cz + D = 0

        // Left plane
        planes[0] = glm::vec4(
            viewProjection[0][3] + viewProjection[0][0],
            viewProjection[1][3] + viewProjection[1][0],
            viewProjection[2][3] + viewProjection[2][0],
            viewProjection[3][3] + viewProjection[3][0]
        );

        // Right plane
        planes[1] = glm::vec4(
            viewProjection[0][3] - viewProjection[0][0],
            viewProjection[1][3] - viewProjection[1][0],
            viewProjection[2][3] - viewProjection[2][0],
            viewProjection[3][3] - viewProjection[3][0]
        );

        // Bottom plane
        planes[2] = glm::vec4(
            viewProjection[0][3] + viewProjection[0][1],
            viewProjection[1][3] + viewProjection[1][1],
            viewProjection[2][3] + viewProjection[2][1],
            viewProjection[3][3] + viewProjection[3][1]
        );

        // Top plane
        planes[3] = glm::vec4(
            viewProjection[0][3] - viewProjection[0][1],
            viewProjection[1][3] - viewProjection[1][1],
            viewProjection[2][3] - viewProjection[2][1],
            viewProjection[3][3] - viewProjection[3][1]
        );

        // Near plane
        planes[4] = glm::vec4(
            viewProjection[0][3] + viewProjection[0][2],
            viewProjection[1][3] + viewProjection[1][2],
            viewProjection[2][3] + viewProjection[2][2],
            viewProjection[3][3] + viewProjection[3][2]
        );

        // Far plane
        planes[5] = glm::vec4(
            viewProjection[0][3] - viewProjection[0][2],
            viewProjection[1][3] - viewProjection[1][2],
            viewProjection[2][3] - viewProjection[2][2],
            viewProjection[3][3] - viewProjection[3][2]
        );

        // Normalize all planes
        for (int i = 0; i < 6; i++) {
            float length = glm::length(glm::vec3(planes[i]));
            planes[i] /= length;
        }
    }

    void GPUDrivenRenderer::updateCameraData(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane)
    {
        glm::mat4 viewProjection = projection * view;

        cameraData.view = view;
        cameraData.projection = projection;
        cameraData.viewProjection = viewProjection;
        cameraData.invViewProjection = glm::inverse(viewProjection);

        cameraData.cameraPosition = glm::vec4(cameraPosition, nearPlane);
        cameraData.screenParams = glm::vec4(
            static_cast<float>(swapChain.getSwapchainExtent().width),
            static_cast<float>(swapChain.getSwapchainExtent().height),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().width),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().height)
        );

        extractFrustumPlanes(viewProjection, cameraData.frustumPlanes);

        cameraData.farPlane = farPlane;
        cameraData.objectCount = mergedBuffer ? mergedBuffer->getObjectCount() : 0;
        cameraData.hiZMipLevels = 0; // Not using Hi-Z in MVP
        cameraData.frameIndex = frameIndex++;

        cameraData.enableFrustumCulling = frustumCullingEnabled ? 1 : 0;
        cameraData.enableOcclusionCulling = 0; // Disabled in MVP
        cameraData.enableLODSelection = lodSelectionEnabled ? 1 : 0;
        cameraData.padding = 0;

        // Copy to GPU
        std::memcpy(cameraMapped, &cameraData, sizeof(GPUCameraData));
    }

    void GPUDrivenRenderer::updateScene(
        const std::vector<mesh::MeshRenderData>& opaqueObjects,
        const mesh::MeshGPUCache& cache,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane)
    {
        if (!initialized || !enabled) {
            return;
        }

        // Register textures for all materials in the scene (done once per material)
        if (materialTextureCache && bindlessTextures) {
            for (const auto& meshRender : opaqueObjects) {
                // Register default material textures
                if (!meshRender.defaultMaterialPath.empty()) {
                    registerMaterialTextures(meshRender.defaultMaterialPath);
                }
                // Register submesh material textures
                for (const auto& [submeshName, subMat] : meshRender.submeshMaterials) {
                    if (!subMat.materialPath.empty()) {
                        registerMaterialTextures(subMat.materialPath);
                    }
                }
            }
        }

        // Create texture resolver callback that uses BindlessTextureManager
        TextureIndexResolver textureResolver = nullptr;
        if (bindlessTextures) {
            textureResolver = [this](const std::string& materialPath, TextureSlotType slot) -> uint32_t {
                if (materialPath.empty()) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Get material data to find texture path
                auto matData = resource::ResourceManager::getMaterial(materialPath);
                if (!matData) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Extract texture paths
                auto pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);

                // Get the appropriate texture path based on slot
                std::string texPath;
                switch (slot) {
                    case TextureSlotType::Albedo:    texPath = pbrValues.albedoTexturePath; break;
                    case TextureSlotType::Normal:    texPath = pbrValues.normalTexturePath; break;
                    case TextureSlotType::ORM:       texPath = pbrValues.ormTexturePath; break;
                    case TextureSlotType::Metallic:  texPath = pbrValues.metallicTexturePath; break;
                    case TextureSlotType::Roughness: texPath = pbrValues.roughnessTexturePath; break;
                    case TextureSlotType::AO:        texPath = pbrValues.aoTexturePath; break;
                    case TextureSlotType::Emission:  texPath = pbrValues.emissionTexturePath; break;
                    case TextureSlotType::Height:    texPath = pbrValues.heightTexturePath; break;
                    default: return INVALID_TEXTURE_INDEX;
                }

                if (texPath.empty()) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Look up the registered texture index
                return bindlessTextures->getTextureIndex(texPath);
            };
        }

        // Update object buffer with current frame's render data
        mergedBuffer->updateObjects(opaqueObjects, cache, textureResolver);

        // Update camera data for compute shader
        updateCameraData(view, projection, cameraPosition, nearPlane, farPlane);

        // Update cull pipeline descriptors
        cullPipeline->updateDescriptors(
            mergedBuffer->getObjectBuffer(),
            cameraBuffer,
            indirectBuffer->getDrawCommandBuffer(),
            indirectBuffer->getPerDrawDataBuffer(),
            indirectBuffer->getDrawCountBuffer()
        );

        // Update per-draw data descriptor for graphics pipeline
        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = indirectBuffer->getPerDrawDataBuffer();
        perDrawInfo.offset = 0;
        perDrawInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet perDrawWrite{};
        perDrawWrite.dstSet = perDrawDataDescriptorSet;
        perDrawWrite.dstBinding = 0;
        perDrawWrite.dstArrayElement = 0;
        perDrawWrite.descriptorCount = 1;
        perDrawWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        perDrawWrite.pBufferInfo = &perDrawInfo;

        device.getLogicalDevice().updateDescriptorSets(perDrawWrite, {});

        // Update stats
        stats.totalObjects = mergedBuffer->getObjectCount();
    }

    void GPUDrivenRenderer::dispatchCompute(vk::CommandBuffer cmd)
    {
        if (!initialized || !enabled || stats.totalObjects == 0)
        {
            return;
        }

        indirectBuffer->resetDrawCount(cmd);
        mergedBuffer->uploadObjects(cmd);

        vk::MemoryBarrier memBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &memBarrier,
            0, nullptr,
            0, nullptr);

        cullPipeline->dispatch(cmd, stats.totalObjects);
        indirectBuffer->insertBarrierAfterCompute(cmd);
    }

    void GPUDrivenRenderer::renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !enabled || stats.totalObjects == 0)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        std::array<vk::DescriptorSet, 3> descriptorSets = {
            iblDescriptorSet,
            perDrawDataDescriptorSet,
            bindlessTextures->getDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            graphicsPipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        vk::Buffer vertBufs[] = { mergedBuffer->getVertexBuffer() };
        vk::DeviceSize vertOffsets[] = { 0 };
        cmd.bindVertexBuffers(0, 1, vertBufs, vertOffsets);
        cmd.bindIndexBuffer(mergedBuffer->getIndexBuffer(), 0, vk::IndexType::eUint32);

        cmd.drawIndexedIndirectCount(
            indirectBuffer->getDrawCommandBuffer(),
            0,
            indirectBuffer->getDrawCountBuffer(),
            0,
            stats.totalObjects,
            sizeof(DrawIndexedIndirectCommand));
    }

    bool GPUDrivenRenderer::registerMaterialTextures(const std::string& materialPath)
    {
        if (!initialized || !bindlessTextures || !materialTextureCache) {
            return false;
        }

        // Skip if already registered
        if (registeredMaterialPaths.contains(materialPath)) {
            return true;
        }

        // Load material data
        auto matData = resource::ResourceManager::getMaterial(materialPath);
        if (!matData) {
            matData = resource::ResourceManager::loadMaterial(materialPath);
        }
        if (!matData) {
            spdlog::warn("GPUDrivenRenderer: Failed to load material: {}", materialPath);
            return false;
        }

        // Extract texture paths from material
        auto pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);

        bool registered = false;

        // Helper lambda to register a texture
        auto tryRegister = [&](const std::string& texPath, const std::string& slotName) {
            if (texPath.empty()) return;

            // Load texture via MaterialTextureCache
            if (!materialTextureCache->loadTexture(texPath)) {
                spdlog::warn("GPUDrivenRenderer: Failed to load texture: {}", texPath);
                return;
            }

            // Get view and sampler
            vk::ImageView view = materialTextureCache->getViewForPath(texPath);
            vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);

            if (view && sampler) {
                uint32_t index = bindlessTextures->registerTexture(texPath, view, sampler);
                spdlog::info("GPUDrivenRenderer: Registered {} texture '{}' at index {}",
                    slotName, texPath, index);
                registered = true;
            }
        };

        // Register all texture slots
        tryRegister(pbrValues.albedoTexturePath, "albedo");
        tryRegister(pbrValues.normalTexturePath, "normal");
        tryRegister(pbrValues.ormTexturePath, "orm");
        tryRegister(pbrValues.metallicTexturePath, "metallic");
        tryRegister(pbrValues.roughnessTexturePath, "roughness");
        tryRegister(pbrValues.aoTexturePath, "ao");
        tryRegister(pbrValues.emissionTexturePath, "emission");
        tryRegister(pbrValues.heightTexturePath, "height");

        // Mark as registered
        registeredMaterialPaths.insert(materialPath);

        return registered;
    }

}
