#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../mesh/MeshStreamManager.hpp"
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
#include <unordered_map>
#include <unordered_set>
#include <memory>

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

        // Create mesh stream manager (streaming enabled by default)
        if (meshStreamingEnabled) {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            spdlog::info("GPUDrivenRenderer: Mesh streaming enabled by default");
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig()) {
            spdlog::error("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            // Fall back to minimal configuration
            if (!batchManager->init(2, 50000, 8)) {
                spdlog::critical("GPUDrivenRenderer: Even minimal batch configuration failed - GPU-driven rendering unavailable");
                batchManager.reset();
            }
        }

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

        // Initialize custom shader cache for GPU-driven compatible material shaders
        customShaderCache = std::make_unique<GPUDrivenShaderCache>(device, swapChain);
        customShaderCache->init(
            iblDescriptorSetLayout,
            perDrawDataLayout,
            bindlessTextures->getDescriptorSetLayout(),
            renderPass
        );

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
        if (customShaderCache) customShaderCache->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        meshStreamManager.reset();
        cullPipeline.reset();
        bindlessTextures.reset();
        batchManager.reset();
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
        float farPlane,
        float time)
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
        cameraData.hiZMipLevels = hiZMipLevels;
        cameraData.frameIndex = frameIndex++;

        cameraData.enableFrustumCulling = frustumCullingEnabled ? 1 : 0;
        cameraData.enableOcclusionCulling = (occlusionCullingEnabled && hiZMipLevels > 0) ? 1 : 0;
        cameraData.enableLODSelection = lodSelectionEnabled ? 1 : 0;
        cameraData.batchCount = batchManager ? batchManager->getBatchCount() : 1;

        cameraData.commandsPerBatch = batchManager ? batchManager->getCommandsPerBatch() : MAX_DRAW_COMMANDS;
        cameraData.shaderGroupCount = batchManager ? batchManager->getShaderGroupCount() : MAX_SHADER_GROUPS;
        cameraData.padding1 = 0;
        cameraData.padding2 = 0;

        // Copy to GPU
        std::memcpy(cameraMapped, &cameraData, sizeof(GPUCameraData));
    }

    void GPUDrivenRenderer::updateScene(
        const std::vector<mesh::MeshRenderData>& opaqueObjects,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane,
        float time)
    {
        if (!initialized || !enabled) {
            return;
        }

        // Update mesh streaming - request all meshes in the scene
        if (meshStreamManager) {
            for (const auto& meshRender : opaqueObjects) {
                meshStreamManager->requestMesh(meshRender.meshPath);
            }

            // Update streaming (process priority queue, upload data)
            glm::mat4 viewProj = projection * view;
            meshStreamManager->update(cameraPosition, viewProj, 0.016f); // Assume ~60fps delta
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
        // Use frame-local cache to avoid re-extracting PBR values for same material multiple times
        TextureIndexResolver textureResolver = nullptr;
        if (bindlessTextures) {
            // Cache extracted PBR values per material path (avoids 8x extraction per material)
            auto pbrCache = std::make_shared<std::unordered_map<std::string, mesh::ExtractedPBRValues>>();

            textureResolver = [this, pbrCache](const std::string& materialPath, TextureSlotType slot) -> uint32_t {
                if (materialPath.empty()) {
                    return INVALID_TEXTURE_INDEX;
                }

                // Check cache first
                auto it = pbrCache->find(materialPath);
                if (it == pbrCache->end()) {
                    // Get or load material data to find texture path
                    auto matData = resource::ResourceManager::getMaterial(materialPath);
                    if (!matData) {
                        // Material weak_ptr expired, reload it
                        matData = resource::ResourceManager::loadMaterial(materialPath);
                    }
                    if (!matData) {
                        return INVALID_TEXTURE_INDEX;
                    }
                    // Extract and cache PBR values (done once per material per frame)
                    it = pbrCache->emplace(materialPath,
                        mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData)).first;
                }

                const auto& pbrValues = it->second;

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

        // Create shader group resolver for custom material shaders
        // Returns 0 for default PBR, 1+ for custom shaders
        ShaderGroupResolver shaderGroupResolver = nullptr;
        if (customShaderCache) {
            // Clear active groups from last frame
            customShaderCache->clearActiveGroups();

            // Cache material data lookups per material path
            auto matDataCache = std::make_shared<std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>>();

            shaderGroupResolver = [this, matDataCache](const std::string& materialPath) -> uint32_t {
                if (materialPath.empty()) {
                    return 0;  // Default shader
                }

                // Check cache first
                auto it = matDataCache->find(materialPath);
                std::shared_ptr<material::MaterialData> matData;
                if (it == matDataCache->end()) {
                    matData = resource::ResourceManager::getMaterial(materialPath);
                    (*matDataCache)[materialPath] = matData;
                } else {
                    matData = it->second;
                }

                if (!matData) {
                    return 0;  // Default shader
                }

                // Check if material has custom shaders
                if (matData->cachedVertexShader.empty() || matData->cachedFragmentShader.empty()) {
                    return 0;  // Default shader
                }

                // Get or create shader group for this custom material
                uint32_t group = customShaderCache->getOrCreateShaderGroup(materialPath, *matData);
                customShaderCache->markGroupActive(group);
                return group;
            };
        }

        // Update object buffer with current frame's render data (pass time for Time node evaluation)
        mergedBuffer->updateObjects(opaqueObjects, textureResolver, shaderGroupResolver, time);

        // Update camera data for compute shader
        updateCameraData(view, projection, cameraPosition, nearPlane, farPlane, time);

        // Update cull pipeline descriptors with combined batch buffers
        cullPipeline->updateDescriptors(
            mergedBuffer->getObjectBuffer(),
            cameraBuffer,
            batchManager->getCombinedDrawCommandBuffer(),
            batchManager->getCombinedPerDrawDataBuffer(),
            batchManager->getCombinedDrawCountBuffer()
        );

        // Update per-draw data descriptor for graphics pipeline (uses combined buffer)
        vk::DescriptorBufferInfo perDrawInfo{};
        perDrawInfo.buffer = batchManager->getCombinedPerDrawDataBuffer();
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
        if (!initialized || !enabled)
        {
            return;
        }

        // Always reset all batch draw counts (clears stale data when no objects)
        batchManager->resetAllBatches(cmd);

        if (stats.totalObjects == 0)
        {
            return;
        }
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
        batchManager->insertBarriersAfterCompute(cmd);
    }

    void GPUDrivenRenderer::renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !enabled || stats.totalObjects == 0)
        {
            return;
        }

        // Bind merged vertex/index buffers once (shared across all pipelines and batches)
        vk::Buffer vertBufs[] = { mergedBuffer->getVertexBuffer() };
        vk::DeviceSize vertOffsets[] = { 0 };
        cmd.bindVertexBuffers(0, 1, vertBufs, vertOffsets);
        cmd.bindIndexBuffer(mergedBuffer->getIndexBuffer(), 0, vk::IndexType::eUint32);

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        // Get active shader groups (always includes group 0 = default)
        const std::set<uint32_t>& activeGroups = customShaderCache ?
            customShaderCache->getActiveGroups() :
            std::set<uint32_t>{0};

        // Multi-pipeline rendering: each shader group has its own buffer sections
        // No fragment discard needed - compute shader outputs to group-specific sections
        for (uint32_t shaderGroup : activeGroups)
        {
            vk::Pipeline pipeline;
            vk::PipelineLayout layout;

            if (shaderGroup == 0) {
                // Default PBR pipeline
                pipeline = graphicsPipeline;
                layout = graphicsPipelineLayout;
            } else {
                // Custom shader pipeline
                if (customShaderCache) {
                    pipeline = customShaderCache->getPipeline(shaderGroup, false);
                }
                if (!pipeline) {
                    spdlog::warn("GPUDrivenRenderer: No pipeline for shader group {}, skipping", shaderGroup);
                    continue;
                }
                // Custom pipelines use the same layout (shared in shader cache)
                layout = graphicsPipelineLayout;
            }

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

            std::array<vk::DescriptorSet, 3> descriptorSets = {
                iblDescriptorSet,
                perDrawDataDescriptorSet,
                bindlessTextures->getDescriptorSet()
            };

            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                layout,
                0,
                static_cast<uint32_t>(descriptorSets.size()),
                descriptorSets.data(),
                0, nullptr);

            // Draw all batches for this shader group
            // Each (batch, shaderGroup) pair has its own section in the buffers
            for (uint32_t batch = 0; batch < batchCount; ++batch)
            {
                // Get offsets for this (batch, shaderGroup) section
                vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                cmd.drawIndexedIndirectCount(
                    batchManager->getCombinedDrawCommandBuffer(),
                    cmdOffset,
                    batchManager->getCombinedDrawCountBuffer(),
                    countOffset,
                    commandsPerSection,
                    sizeof(DrawIndexedIndirectCommand));
            }
        }
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

        // Load material data and cache it to prevent weak_ptr expiration
        auto matData = resource::ResourceManager::getMaterial(materialPath);
        if (!matData) {
            matData = resource::ResourceManager::loadMaterial(materialPath);
        }
        if (!matData) {
            spdlog::warn("GPUDrivenRenderer: Failed to load material: {}", materialPath);
            return false;
        }
        // Keep material alive by storing in our cache
        loadedMaterials[materialPath] = matData;

        // Extract texture paths from material
        auto pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);

        bool registered = false;

        // Helper lambda to register a texture
        auto tryRegister = [&](const std::string& texPath) {
            if (texPath.empty()) return;

            // Load texture via MaterialTextureCache
            if (!materialTextureCache->loadTexture(texPath)) {
                return;
            }

            // Get view and sampler
            vk::ImageView view = materialTextureCache->getViewForPath(texPath);
            vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);

            if (view && sampler) {
                bindlessTextures->registerTexture(texPath, view, sampler);
                registered = true;
            }
        };

        // Register all texture slots
        tryRegister(pbrValues.albedoTexturePath);
        tryRegister(pbrValues.normalTexturePath);
        tryRegister(pbrValues.ormTexturePath);
        tryRegister(pbrValues.metallicTexturePath);
        tryRegister(pbrValues.roughnessTexturePath);
        tryRegister(pbrValues.aoTexturePath);
        tryRegister(pbrValues.emissionTexturePath);
        tryRegister(pbrValues.heightTexturePath);

        // Only mark as registered if at least one texture was successfully registered
        if (registered) {
            registeredMaterialPaths.insert(materialPath);
        }
        // If no textures were registered, don't add to registeredMaterialPaths - allow retry on next frame

        return registered;
    }

    void GPUDrivenRenderer::updateHiZPyramid(vk::ImageView hiZView, vk::Sampler hiZSampler, uint32_t mipLevels)
    {
        if (!initialized) {
            return;
        }

        bool wasAvailable = (hiZMipLevels > 0);

        cachedHiZView = hiZView;
        cachedHiZSampler = hiZSampler;
        hiZMipLevels = mipLevels;

        // Update the cull pipeline descriptor with Hi-Z texture
        if (cullPipeline && hiZView && hiZSampler) {
            cullPipeline->updateHiZDescriptor(hiZView, hiZSampler);

            // Auto-enable occlusion culling when Hi-Z first becomes available
            if (!wasAvailable && mipLevels > 0 && !occlusionCullingEnabled) {
                occlusionCullingEnabled = true;
                spdlog::info("GPUDrivenRenderer: Hi-Z occlusion culling enabled ({} mip levels)", mipLevels);
            }
        }
    }

    uint32_t GPUDrivenRenderer::getMergedVertexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalVertexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getMergedIndexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalIndexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredMeshCount() const
    {
        return mergedBuffer ? static_cast<uint32_t>(mergedBuffer->getRegisteredMeshes().size()) : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredTextureCount() const
    {
        return bindlessTextures ? bindlessTextures->getRegisteredTextureCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getBatchCount() const
    {
        return batchManager ? batchManager->getBatchCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getCommandsPerBatch() const
    {
        return batchManager ? batchManager->getCommandsPerBatch() : 0;
    }

    uint32_t GPUDrivenRenderer::getTotalCapacity() const
    {
        return batchManager ? batchManager->getTotalCapacity() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCommandBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCommandBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCountBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCountBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getPerDrawDataBufferSize() const
    {
        return batchManager ? batchManager->getCombinedPerDrawDataBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getTotalMemoryUsage() const
    {
        if (!batchManager) return 0;
        return batchManager->getCombinedDrawCommandBufferSize() +
               batchManager->getCombinedDrawCountBufferSize() +
               batchManager->getCombinedPerDrawDataBufferSize();
    }

    void GPUDrivenRenderer::updateStatsFromGPU()
    {
        if (!initialized || !enabled || !batchManager) {
            return;
        }

        // Read back and aggregate stats from all batches (expensive - causes sync)
        GPUDrivenStats aggregated = batchManager->readBackAggregatedStats();

        stats.visibleObjects = aggregated.visibleObjects;
        stats.drawCalls = aggregated.drawCalls;  // One draw call per batch

        // LOD distribution from GPU (aggregated across all batches)
        stats.objectsLOD0 = aggregated.objectsLOD0;
        stats.objectsLOD1 = aggregated.objectsLOD1;
        stats.objectsLOD2 = aggregated.objectsLOD2;
        stats.objectsLOD3 = aggregated.objectsLOD3;

        // Culling stats directly from GPU counters (aggregated)
        stats.culledByFrustum = aggregated.culledByFrustum;
        stats.culledByOcclusion = aggregated.culledByOcclusion;
    }

    // ===== MESH STREAMING SUPPORT =====

    void GPUDrivenRenderer::setMeshStreamingEnabled(bool enabled)
    {
        if (enabled == meshStreamingEnabled) {
            return;
        }

        meshStreamingEnabled = enabled;

        if (enabled && mergedBuffer && !meshStreamManager) {
            // Create streaming manager
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            spdlog::info("GPUDrivenRenderer: Mesh streaming enabled");
        } else if (!enabled && meshStreamManager) {
            // Destroy streaming manager
            meshStreamManager.reset();
            spdlog::info("GPUDrivenRenderer: Mesh streaming disabled");
        }
    }

    MergedMeshBuffer::StreamingStats GPUDrivenRenderer::getStreamingStats() const
    {
        if (mergedBuffer) {
            return mergedBuffer->getStreamingStats();
        }
        return {};
    }

    void GPUDrivenRenderer::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        // Check if anything actually changed
        bool renderPassChanged = (cachedRenderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && cachedIBLLayout != newIBLLayout);

        if (!renderPassChanged && !iblLayoutChanged) return;

        spdlog::info("GPUDrivenRenderer: Updating render pass/IBL layout, recreating pipelines");

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Destroy old graphics pipeline and layout
        if (graphicsPipeline) {
            vkDevice.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }
        if (graphicsPipelineLayout) {
            vkDevice.destroyPipelineLayout(graphicsPipelineLayout);
            graphicsPipelineLayout = nullptr;
        }

        // Clean up old shader
        if (meshShader) {
            meshShader->cleanUp();
            meshShader.reset();
        }

        // Update cached values
        cachedRenderPass = newRenderPass;
        if (newIBLLayout) {
            cachedIBLLayout = newIBLLayout;
        }

        // Recreate main graphics pipeline with new render pass and IBL layout
        createGraphicsPipeline(cachedIBLLayout, cachedRenderPass);

        // Update custom shader cache with new render pass and IBL layout
        if (customShaderCache) {
            customShaderCache->updateRenderPass(newRenderPass, newIBLLayout);
        }
    }

}
