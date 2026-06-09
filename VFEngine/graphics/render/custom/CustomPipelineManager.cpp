#include "CustomPipelineManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "resource/PathResolver.hpp"
#include "print/Log.hpp"

namespace render::custom
{
    namespace
    {
        // Built-in push-constant block at offset 0, user data follows. Unlit
        // pipelines receive the MVP here; lit pipelines receive the MODEL matrix
        // and reconstruct clip position from the camera UBO (set 0).
        constexpr uint32_t BUILTIN_PUSH_CONSTANT_SIZE = sizeof(glm::mat4);

        // Lit pipelines mirror the engine scene-pass set numbering (sets 0-10).
        constexpr uint32_t LIT_DESCRIPTOR_SET_COUNT = 11;

        // With RT shadows online the layout extends to set 13 (RT shadow mask);
        // sets 11-12 are empty placeholders like 1-5.
        constexpr uint32_t LIT_DESCRIPTOR_SET_COUNT_WITH_RT = 14;

        // Binds contiguous runs of non-null sets; null slots (placeholders 1-5,
        // uninitialized shadow sets) are skipped. Mirrors the terrain pipeline's
        // bindDescriptorSetsInBatches.
        void bindSetsSkippingNulls(const vk::CommandBuffer& commandBuffer, vk::PipelineLayout layout,
                                   const vk::DescriptorSet* sets, uint32_t count)
        {
            uint32_t batchStart = 0;
            std::vector<vk::DescriptorSet> batch;

            auto flush = [&]()
            {
                if (batch.empty()) return;
                commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
                                                 batchStart, static_cast<uint32_t>(batch.size()),
                                                 batch.data(), 0, nullptr);
                batch.clear();
            };

            for (uint32_t i = 0; i < count; ++i)
            {
                if (!sets[i])
                {
                    flush();
                    continue;
                }
                if (batch.empty()) batchStart = i;
                batch.push_back(sets[i]);
            }
            flush();
        }
    }

    CustomPipelineManager::CustomPipelineManager(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    CustomPipelineManager::~CustomPipelineManager() = default;

    uint32_t CustomPipelineManager::attributeSize(plugin::CustomVertexAttribute attribute)
    {
        switch (attribute)
        {
        case plugin::CustomVertexAttribute::Float:  return 4;
        case plugin::CustomVertexAttribute::Float2: return 8;
        case plugin::CustomVertexAttribute::Float3: return 12;
        case plugin::CustomVertexAttribute::Float4: return 16;
        }
        return 0;
    }

    vk::Format CustomPipelineManager::attributeFormat(plugin::CustomVertexAttribute attribute)
    {
        switch (attribute)
        {
        case plugin::CustomVertexAttribute::Float:  return vk::Format::eR32Sfloat;
        case plugin::CustomVertexAttribute::Float2: return vk::Format::eR32G32Sfloat;
        case plugin::CustomVertexAttribute::Float3: return vk::Format::eR32G32B32Sfloat;
        case plugin::CustomVertexAttribute::Float4: return vk::Format::eR32G32B32A32Sfloat;
        }
        return vk::Format::eUndefined;
    }

    plugin::CustomPipelineHandle CustomPipelineManager::createPipeline(const plugin::CustomPipelineDesc& desc)
    {
        if (desc.glslSource.empty())
        {
            vfLogError("CustomPipelineManager: pipeline rejected — empty GLSL source");
            return {};
        }
        if (desc.vertexLayout.empty())
        {
            vfLogError("CustomPipelineManager: pipeline rejected — empty vertex layout");
            return {};
        }
        if (desc.pushConstantSize > plugin::MAX_CUSTOM_PUSH_CONSTANT_SIZE)
        {
            vfLogError("CustomPipelineManager: pipeline rejected — pushConstantSize {} exceeds max {}",
                       desc.pushConstantSize, plugin::MAX_CUSTOM_PUSH_CONSTANT_SIZE);
            return {};
        }

        PipelineEntry entry;
        entry.desc = desc;
        entry.shader = std::make_shared<core::Shader>(device);

        // Plugin shaders may #include the shared chunks under resources/shaders/
        // (e.g. common/lighting_functions.glsl for lit pipelines).
        entry.shader->setIncludeBasePath(resource::PathResolver::resolveEnginePath("../../resources/shaders"));

        if (!entry.shader->compileFromSource(desc.glslSource, "plugin_custom_pipeline"))
        {
            vfLogError("CustomPipelineManager: shader compilation failed: {}",
                       entry.shader->getLastCompilationError());
            return {};
        }

        if (desc.receiveLighting && !lightingLayouts.isComplete())
        {
            // Plugins can create pipelines before the gpu-driven renderer's
            // lighting managers exist — keep the entry and build the pipeline
            // when setLightingLayouts() arrives. Draws are skipped until then.
            uint64_t id = nextId++;
            vfLogInfo("CustomPipelineManager: deferring lit pipeline {} until lighting layouts are available", id);
            pipelines.emplace(id, std::move(entry));
            return plugin::CustomPipelineHandle{id};
        }

        if (!buildPipeline(entry))
        {
            return {};
        }

        uint64_t id = nextId++;
        pipelines.emplace(id, std::move(entry));
        return plugin::CustomPipelineHandle{id};
    }

    void CustomPipelineManager::setLightingLayouts(const CustomLightingLayouts& layouts)
    {
        lightingLayouts = layouts;
        if (!lightingLayouts.isComplete()) return;

        for (auto& [id, entry] : pipelines)
        {
            if (entry.desc.receiveLighting && !entry.pipeline)
            {
                if (!buildPipeline(entry))
                {
                    vfLogError("CustomPipelineManager: deferred lit pipeline {} build failed", id);
                }
            }
        }
    }

    void CustomPipelineManager::setRTShadowMaskLayout(vk::DescriptorSetLayout layout)
    {
        if (layout == lightingLayouts.rtShadowMask) return;
        lightingLayouts.rtShadowMask = layout;

        // Layout gone (RT shadows torn down): keep pipelines as-built — the
        // runtime flag lightCounts.rtShadowActive gates the shader branch,
        // mirroring the engine mesh/terrain pipelines which never rebuild down.
        if (!layout) return;
        if (!lightingLayouts.isComplete()) return;

        bool anyLit = false;
        for (const auto& [id, entry] : pipelines)
        {
            if (entry.desc.receiveLighting)
            {
                anyLit = true;
                break;
            }
        }
        if (!anyLit) return;

        // Fires once per RT-enable (and once more on a raw -> denoised layout
        // switch) — never on the per-frame unchanged path.
        device.getLogicalDevice().waitIdle();
        for (auto& [id, entry] : pipelines)
        {
            if (!entry.desc.receiveLighting) continue;
            destroyPipelineObjects(entry);
            if (!buildPipeline(entry))
            {
                vfLogError("CustomPipelineManager: lit pipeline {} rebuild with RT shadow mask failed", id);
            }
        }
        vfLogInfo("CustomPipelineManager: lit pipelines rebuilt with RT shadow mask (set 13)");
    }

    bool CustomPipelineManager::recompileShaderWithRTMacro(PipelineEntry& entry)
    {
        // compileFromSource appends shader stages, so a recompile needs a fresh
        // core::Shader rather than compiling on the existing one.
        auto shader = std::make_shared<core::Shader>(device);
        shader->setIncludeBasePath(resource::PathResolver::resolveEnginePath("../../resources/shaders"));
        shader->addMacroDefinition("RT_SHADOW_ENABLED");
        if (!shader->compileFromSource(entry.desc.glslSource, "plugin_custom_pipeline"))
        {
            vfLogError("CustomPipelineManager: RT_SHADOW_ENABLED recompile failed: {}",
                       shader->getLastCompilationError());
            return false;
        }
        if (entry.shader) entry.shader->cleanUp();
        entry.shader = std::move(shader);
        entry.shaderHasRTMacro = true;
        return true;
    }

    bool CustomPipelineManager::buildPipeline(PipelineEntry& entry)
    {
        const auto& desc = entry.desc;

        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> attributes;
        attributes.reserve(desc.vertexLayout.size());
        uint32_t offset = 0;
        for (uint32_t location = 0; location < desc.vertexLayout.size(); ++location)
        {
            vk::VertexInputAttributeDescription attribute{};
            attribute.binding = 0;
            attribute.location = location;
            attribute.format = attributeFormat(desc.vertexLayout[location]);
            attribute.offset = offset;
            attributes.push_back(attribute);
            offset += attributeSize(desc.vertexLayout[location]);
        }
        binding.stride = offset;

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {swapChain.getSceneColorFormat()};
        config.depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat();
        config.extent = swapChain.getSwapchainExtent();
        // Plugin custom pipelines draw in the scene pass — sample count set per pass.
        config.dynamicSampleCount = true;
        config.shaderStages = entry.shader->getShaderStages();
        config.vertexBindings = {binding};
        config.vertexAttributes = attributes;

        if (desc.receiveLighting)
        {
            if (!lightingLayouts.isComplete())
            {
                vfLogError("CustomPipelineManager: lit pipeline build requested before lighting layouts are set");
                return false;
            }
            if (!emptyLayout)
            {
                emptyLayout = device.getLogicalDevice().createDescriptorSetLayout({});
            }
            // Engine scene-pass set numbering; sets 1-5 are empty placeholders.
            config.descriptorSetLayouts = {
                lightingLayouts.ibl,                                            // set 0: camera + IBL
                emptyLayout, emptyLayout, emptyLayout, emptyLayout, emptyLayout, // sets 1-5
                lightingLayouts.lights,                                         // set 6
                lightingLayouts.clusterParams,                                  // set 7
                lightingLayouts.clusterIndices,                                 // set 8
                lightingLayouts.shadowData,                                     // set 9
                lightingLayouts.shadowTextures                                  // set 10
            };

            if (lightingLayouts.rtShadowMask)
            {
                // RT shadows online: inject RT_SHADOW_ENABLED so plugin shaders
                // can opt in via #ifdef, and extend the layout to set 13.
                if (!entry.shaderHasRTMacro && !recompileShaderWithRTMacro(entry))
                {
                    return false;
                }
                config.shaderStages = entry.shader->getShaderStages();
                config.descriptorSetLayouts.push_back(emptyLayout);                // set 11
                config.descriptorSetLayouts.push_back(emptyLayout);                // set 12
                config.descriptorSetLayouts.push_back(lightingLayouts.rtShadowMask); // set 13
                entry.hasRTShadowSet = true;
            }
            else
            {
                entry.hasRTShadowSet = false;
            }
        }

        config.pushConstantSize = BUILTIN_PUSH_CONSTANT_SIZE + desc.pushConstantSize;
        config.pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = desc.depthTest;
        config.depthWriteEnable = desc.depthWrite;
        config.depthCompareOp = vk::CompareOp::eLessOrEqual;
        config.blendEnable = desc.blendMode == plugin::CustomBlendMode::AlphaBlend;

        switch (desc.cullMode)
        {
        case plugin::CustomCullMode::None:  config.cullMode = vk::CullModeFlagBits::eNone; break;
        case plugin::CustomCullMode::Back:  config.cullMode = vk::CullModeFlagBits::eBack; break;
        case plugin::CustomCullMode::Front: config.cullMode = vk::CullModeFlagBits::eFront; break;
        }

        switch (desc.topology)
        {
        case plugin::CustomTopology::TriangleList: config.topology = vk::PrimitiveTopology::eTriangleList; break;
        case plugin::CustomTopology::LineList:     config.topology = vk::PrimitiveTopology::eLineList; break;
        }

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        if (!result.pipeline)
        {
            vfLogError("CustomPipelineManager: pipeline creation failed");
            return false;
        }

        entry.pipeline = result.pipeline;
        entry.layout = result.pipelineLayout;
        return true;
    }

    plugin::CustomMeshHandle CustomPipelineManager::uploadMesh(plugin::CustomMeshData&& data)
    {
        if (data.vertexData.empty())
        {
            vfLogError("CustomPipelineManager: mesh rejected — empty vertex data");
            return {};
        }
        if (data.indices.empty() && data.vertexCount == 0)
        {
            vfLogError("CustomPipelineManager: mesh rejected — no indices and vertexCount is 0");
            return {};
        }

        MeshEntry entry;
        entry.vertexCount = data.vertexCount;
        entry.indexCount = static_cast<uint32_t>(data.indices.size());

        const vk::DeviceSize vertexSize = static_cast<vk::DeviceSize>(data.vertexData.size());
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, entry.vertexBuffer, entry.vertexAllocation,
                                            device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            entry.vertexBuffer,
            data.vertexData.data(),
            vertexSize);

        if (entry.indexCount > 0)
        {
            const vk::DeviceSize indexSize = static_cast<vk::DeviceSize>(data.indices.size() * sizeof(uint32_t));
            core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexRequest.size = indexSize;
            indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(indexRequest, entry.indexBuffer, entry.indexAllocation,
                                                device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                device.getStagingCommandPool(),
                entry.indexBuffer,
                data.indices.data(),
                indexSize);
        }

        uint64_t id = nextId++;
        meshes.emplace(id, entry);
        return plugin::CustomMeshHandle{id};
    }

    void CustomPipelineManager::enqueueDraw(plugin::CustomDrawItem&& item)
    {
        if (!pipelines.contains(item.pipeline.id) || !meshes.contains(item.mesh.id))
        {
            return;
        }
        pendingDraws.push_back(std::move(item));
    }

    void CustomPipelineManager::render(const vk::CommandBuffer& commandBuffer,
                                       const glm::mat4& view, const glm::mat4& projection,
                                       const CustomLightingSets& lightingSets) const
    {
        if (pendingDraws.empty()) return;

        constexpr auto pushStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

        for (const auto& item : pendingDraws)
        {
            auto pipelineIt = pipelines.find(item.pipeline.id);
            auto meshIt = meshes.find(item.mesh.id);
            if (pipelineIt == pipelines.end() || meshIt == meshes.end()) continue;

            const PipelineEntry& pipelineEntry = pipelineIt->second;
            const MeshEntry& meshEntry = meshIt->second;

            // Deferred lit pipeline whose layouts never arrived (or failed to build).
            if (!pipelineEntry.pipeline) continue;

            const bool lit = pipelineEntry.desc.receiveLighting;
            // Lit shaders statically access the lighting sets — drawing while any
            // is unbound is invalid, so skip until the frame provides all of them.
            if (lit && !lightingSets.isComplete()) continue;

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineEntry.pipeline);

            vk::Buffer vertexBuffers[] = {meshEntry.vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            if (lit)
            {
                const vk::DescriptorSet sets[LIT_DESCRIPTOR_SET_COUNT_WITH_RT] = {
                    lightingSets.ibl,
                    {}, {}, {}, {}, {},          // sets 1-5: empty placeholders
                    lightingSets.lights,
                    lightingSets.clusterParams,
                    lightingSets.clusterIndices,
                    lightingSets.shadowData,
                    lightingSets.shadowTextures,
                    {}, {},                      // sets 11-12: empty placeholders
                    lightingSets.rtShadowMask    // set 13: only with RT shadows online
                };
                const uint32_t setCount = pipelineEntry.hasRTShadowSet
                    ? LIT_DESCRIPTOR_SET_COUNT_WITH_RT
                    : LIT_DESCRIPTOR_SET_COUNT;
                bindSetsSkippingNulls(commandBuffer, pipelineEntry.layout, sets, setCount);

                // Lit pipelines receive the model matrix; the shader reconstructs
                // clip position from the camera UBO.
                commandBuffer.pushConstants(pipelineEntry.layout, pushStages,
                                            0, BUILTIN_PUSH_CONSTANT_SIZE, &item.model);
            }
            else
            {
                const glm::mat4 mvp = projection * view * item.model;
                commandBuffer.pushConstants(pipelineEntry.layout, pushStages,
                                            0, BUILTIN_PUSH_CONSTANT_SIZE, &mvp);
            }

            const uint32_t userSize = pipelineEntry.desc.pushConstantSize;
            if (userSize > 0 && item.pushConstants.size() >= userSize)
            {
                commandBuffer.pushConstants(pipelineEntry.layout, pushStages,
                                            BUILTIN_PUSH_CONSTANT_SIZE, userSize, item.pushConstants.data());
            }

            if (meshEntry.indexCount > 0)
            {
                commandBuffer.bindIndexBuffer(meshEntry.indexBuffer, 0, vk::IndexType::eUint32);
                commandBuffer.drawIndexed(meshEntry.indexCount, 1, 0, 0, 0);
                render::FrameDrawStats::count(render::DrawCategory::Custom);
            }
            else
            {
                commandBuffer.draw(meshEntry.vertexCount, 1, 0, 0);
                render::FrameDrawStats::count(render::DrawCategory::Custom);
            }
        }
    }

    void CustomPipelineManager::endFrame()
    {
        pendingDraws.clear();
    }

    void CustomPipelineManager::recreatePipelines()
    {
        for (auto& [id, entry] : pipelines)
        {
            destroyPipelineObjects(entry);
            // Lit pipelines stay deferred until lighting layouts are available.
            if (entry.desc.receiveLighting && !lightingLayouts.isComplete()) continue;
            buildPipeline(entry);
        }
    }

    void CustomPipelineManager::destroyPipeline(plugin::CustomPipelineHandle handle)
    {
        auto it = pipelines.find(handle.id);
        if (it == pipelines.end()) return;

        // Rare operation (plugin unload) — wait so no in-flight frame uses the pipeline.
        device.getLogicalDevice().waitIdle();
        destroyPipelineObjects(it->second);
        if (it->second.shader) it->second.shader->cleanUp();
        pipelines.erase(it);

        std::erase_if(pendingDraws, [&](const plugin::CustomDrawItem& item)
        {
            return item.pipeline.id == handle.id;
        });
    }

    void CustomPipelineManager::destroyMesh(plugin::CustomMeshHandle handle)
    {
        auto it = meshes.find(handle.id);
        if (it == meshes.end()) return;

        device.getLogicalDevice().waitIdle();
        destroyMeshBuffers(it->second);
        meshes.erase(it);

        std::erase_if(pendingDraws, [&](const plugin::CustomDrawItem& item)
        {
            return item.mesh.id == handle.id;
        });
    }

    void CustomPipelineManager::destroyPipelineObjects(PipelineEntry& entry)
    {
        auto& logicalDevice = device.getLogicalDevice();
        if (entry.pipeline)
        {
            logicalDevice.destroyPipeline(entry.pipeline);
            entry.pipeline = nullptr;
        }
        if (entry.layout)
        {
            logicalDevice.destroyPipelineLayout(entry.layout);
            entry.layout = nullptr;
        }
    }

    void CustomPipelineManager::destroyMeshBuffers(MeshEntry& entry)
    {
        auto& logicalDevice = device.getLogicalDevice();
        if (entry.vertexBuffer)
        {
            logicalDevice.destroyBuffer(entry.vertexBuffer);
            entry.vertexBuffer = nullptr;
        }
        if (entry.vertexAllocation)
        {
            device.getMemoryManager().free(entry.vertexAllocation);
            entry.vertexAllocation = {};
        }
        if (entry.indexBuffer)
        {
            logicalDevice.destroyBuffer(entry.indexBuffer);
            entry.indexBuffer = nullptr;
        }
        if (entry.indexAllocation)
        {
            device.getMemoryManager().free(entry.indexAllocation);
            entry.indexAllocation = {};
        }
    }

    void CustomPipelineManager::cleanUp()
    {
        pendingDraws.clear();

        for (auto& [id, entry] : pipelines)
        {
            destroyPipelineObjects(entry);
            if (entry.shader) entry.shader->cleanUp();
        }
        pipelines.clear();

        for (auto& [id, entry] : meshes)
        {
            destroyMeshBuffers(entry);
        }
        meshes.clear();

        if (emptyLayout)
        {
            device.getLogicalDevice().destroyDescriptorSetLayout(emptyLayout);
            emptyLayout = nullptr;
        }
    }
}
