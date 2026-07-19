#include "StaticMeshPipeline.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialParameterBufferCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::mesh
{
    void StaticMeshPipeline::createDescriptorSetLayout()
    {
        // Bindings 0-3: camera UBO + the global IBL triple (irradiance, prefilter, BRDF LUT).
        // Bindings 4-5: VK-1577 reflection probes (cube slots + metadata SSBO).
        //
        // Probes live here rather than on a set of their own because the set budget is exhausted:
        // selection coverage already occupies set 15 (MeshShaderPipeline.cpp:741-747), so a new set
        // would need 17 bound sets on a path where RT spot/point shadows are already gated at 15
        // (GPUDrivenRendererGI.cpp:654). Set 0 is also semantically right — probes ARE image-based
        // lighting, and every surface that samples IBL (mesh, terrain, water, vegetation, billboard,
        // depth prepass) already receives this one layout.
        std::vector<vk::DescriptorSetLayoutBinding> bindings(6);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[2].pImmutableSamplers = nullptr;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[3].pImmutableSamplers = nullptr;

        // VK-1577 binding 4: an ARRAY of MAX_REFLECTION_PROBES combined image samplers, not a
        // samplerCubeArray. A descriptor array needs no new device feature; a cube array would
        // require `imageCubeArray`, which this device is not created with (Device.cpp:374-407).
        // Every slot is always written, so no ePartiallyBound flag is needed.
        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = probe::MAX_REFLECTION_PROBES;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[4].pImmutableSamplers = nullptr;

        // VK-1577 binding 5: probe metadata SSBO ({ count header, GPUReflectionProbe[MAX] }).
        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[5].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createDescriptorPool()
    {
        std::vector<vk::DescriptorPoolSize> poolSizes(3);
        poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
        // irradiance, prefilter, brdfLUT + VK-1577 probe cube slots
        poolSizes[1].descriptorCount = 3 + probe::MAX_REFLECTION_PROBES;
        poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[2].descriptorCount = 1; // VK-1577 probe metadata

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::createEmptyProbeBuffer()
    {
        if (emptyProbeBuffer) return;

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        // Full size, not just the header: a zero count means the shader never indexes the array,
        // but keeping it full-size means even a buggy read stays in bounds.
        bufferRequest.size = probe::PROBE_BUFFER_SIZE;
        core::BufferUtilities::createBuffer(bufferRequest, emptyProbeBuffer, emptyProbeAllocation,
                                            device.getMemoryManager());

        if (emptyProbeAllocation.mappedPtr)
        {
            std::memset(emptyProbeAllocation.mappedPtr, 0, probe::PROBE_BUFFER_SIZE);
        }
    }

    void StaticMeshPipeline::writeProbeBindings(vk::DescriptorSet target, vk::Buffer probeBuffer) const
    {
        // Binding 4 — every slot gets a valid descriptor. Unallocated slots point at the global
        // prefilter map so an un-baked probe degrades to the sky rather than sampling garbage; that
        // image is already the right format with 5 mips and a CLAMP_NONE sampler.
        std::array<vk::DescriptorImageInfo, probe::MAX_REFLECTION_PROBES> cubeInfos{};
        for (uint32_t i = 0; i < probe::MAX_REFLECTION_PROBES; ++i)
        {
            const ibl::ImageData& src = cachedProbeCubes[i].imageView ? cachedProbeCubes[i]
                                                                     : cachedPrefilterMap;
            cubeInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cubeInfos[i].imageView = src.imageView;
            cubeInfos[i].sampler = src.sampler;
        }

        vk::WriteDescriptorSet cubeWrite{};
        cubeWrite.dstSet = target;
        cubeWrite.dstBinding = 4;
        cubeWrite.dstArrayElement = 0;
        cubeWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        cubeWrite.descriptorCount = probe::MAX_REFLECTION_PROBES;
        cubeWrite.pImageInfo = cubeInfos.data();

        vk::DescriptorBufferInfo probeInfo{};
        probeInfo.buffer = probeBuffer ? probeBuffer : emptyProbeBuffer;
        probeInfo.offset = 0;
        probeInfo.range = probe::PROBE_BUFFER_SIZE;

        vk::WriteDescriptorSet probeWrite{};
        probeWrite.dstSet = target;
        probeWrite.dstBinding = 5;
        probeWrite.dstArrayElement = 0;
        probeWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        probeWrite.descriptorCount = 1;
        probeWrite.pBufferInfo = &probeInfo;

        std::array<vk::WriteDescriptorSet, 2> writes = {cubeWrite, probeWrite};
        device.getLogicalDevice().updateDescriptorSets(writes, nullptr);
    }

    void StaticMeshPipeline::setReflectionProbeResources(
        vk::Buffer probeBuffer,
        const std::array<ibl::ImageData, probe::MAX_REFLECTION_PROBES>& cubes)
    {
        cachedProbeBuffer = probeBuffer;
        cachedProbeCubes = cubes;

        if (descriptorSet)
        {
            writeProbeBindings(descriptorSet, cachedProbeBuffer);
        }
    }

    void StaticMeshPipeline::createCameraUBO()
    {
        if (externalCameraBuffer) return;

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(CameraUBO);
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
    }

    void StaticMeshPipeline::createDescriptorSet(const ibl::ImageData& irradianceMap,
                                                 const ibl::ImageData& prefilterMap,
                                                 const ibl::ImageData& brdfLUT)
    {
        cachedIrradianceMap = irradianceMap;
        cachedPrefilterMap = prefilterMap;
        cachedBrdfLUT = brdfLUT;
        ++iblDescriptorVersion;

        // VK-1577: bindings 4/5 must be writable below; idempotent, so safe on every reinit.
        createEmptyProbeBuffer();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

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

        // VK-1577: bindings 4/5. cachedPrefilterMap was just refreshed above, so unallocated probe
        // slots pick up the new global environment too.
        writeProbeBindings(descriptorSet, cachedProbeBuffer);
    }

    vk::DescriptorSet StaticMeshPipeline::createExternalIBLDescriptorSet(vk::Buffer externalCameraUBO,
                                                                          vk::DescriptorPool externalPool) const
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = externalPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        vk::DescriptorSet outSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = externalCameraUBO;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(CameraUBO);

        vk::WriteDescriptorSet uboWrite{};
        uboWrite.dstSet = outSet;
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &uboBufferInfo;

        vk::DescriptorImageInfo irradianceImageInfo{};
        irradianceImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        irradianceImageInfo.imageView = cachedIrradianceMap.imageView;
        irradianceImageInfo.sampler = cachedIrradianceMap.sampler;

        vk::WriteDescriptorSet irradianceWrite{};
        irradianceWrite.dstSet = outSet;
        irradianceWrite.dstBinding = 1;
        irradianceWrite.dstArrayElement = 0;
        irradianceWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        irradianceWrite.descriptorCount = 1;
        irradianceWrite.pImageInfo = &irradianceImageInfo;

        vk::DescriptorImageInfo prefilterImageInfo{};
        prefilterImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        prefilterImageInfo.imageView = cachedPrefilterMap.imageView;
        prefilterImageInfo.sampler = cachedPrefilterMap.sampler;

        vk::WriteDescriptorSet prefilterWrite{};
        prefilterWrite.dstSet = outSet;
        prefilterWrite.dstBinding = 2;
        prefilterWrite.dstArrayElement = 0;
        prefilterWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        prefilterWrite.descriptorCount = 1;
        prefilterWrite.pImageInfo = &prefilterImageInfo;

        vk::DescriptorImageInfo brdfImageInfo{};
        brdfImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        brdfImageInfo.imageView = cachedBrdfLUT.imageView;
        brdfImageInfo.sampler = cachedBrdfLUT.sampler;

        vk::WriteDescriptorSet brdfWrite{};
        brdfWrite.dstSet = outSet;
        brdfWrite.dstBinding = 3;
        brdfWrite.dstArrayElement = 0;
        brdfWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        brdfWrite.descriptorCount = 1;
        brdfWrite.pImageInfo = &brdfImageInfo;

        std::array<vk::WriteDescriptorSet, 4> descriptorWrites = {
            uboWrite, irradianceWrite, prefilterWrite, brdfWrite
        };
        device.getLogicalDevice().updateDescriptorSets(descriptorWrites, nullptr);

        // VK-1577: bindings 4/5, deliberately with the EMPTY probe buffer (count = 0).
        //
        // This is the capture-feedback guard, not an optimization. A probe bake re-renders the scene
        // through exactly this descriptor set while rendering INTO the cubes bound at binding 4;
        // sampling an image the same submit writes is undefined and device-lost-class, not a visual
        // artifact. A zero count means the shader's probe loop never executes, so the cubes are bound
        // but never read. Preview and render-texture passes get the same neutral treatment for free.
        writeProbeBindings(outSet, emptyProbeBuffer);

        return outSet;
    }

    void StaticMeshPipeline::createTextureDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureBinding.descriptorCount = material::MAX_MATERIAL_TEXTURES;
        textureBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
        textureBinding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;

        textureDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void StaticMeshPipeline::createTextureDescriptorPool()
    {
        const uint32_t imageCount = static_cast<uint32_t>(swapChain.getImageCount());

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = material::MAX_MATERIAL_TEXTURES * imageCount;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = imageCount;

        textureDescriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void StaticMeshPipeline::initializeDefaultTextureDescriptors()
    {
        textureCache->initDescriptorResources(textureDescriptorSetLayout);

        if (!textureCache->hasDefaultTexture())
        {
            vfLogWarning("Default texture not available, skipping default descriptor set creation");
            textureDescriptorsInitialized = false;
            return;
        }

        if (textureDescriptorSets.empty())
        {
            const uint32_t imageCount = static_cast<uint32_t>(swapChain.getImageCount());
            std::vector<vk::DescriptorSetLayout> layouts(imageCount, textureDescriptorSetLayout);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = textureDescriptorPool;
            allocInfo.descriptorSetCount = imageCount;
            allocInfo.pSetLayouts = layouts.data();
            textureDescriptorSets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);

            std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;
            for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
            {
                imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfos[i].imageView = textureCache->getDefaultView();
                imageInfos[i].sampler = textureCache->getDefaultSampler();
            }

            std::vector<vk::WriteDescriptorSet> writes;
            writes.reserve(textureDescriptorSets.size());
            for (vk::DescriptorSet set : textureDescriptorSets)
            {
                vk::WriteDescriptorSet writeSet{};
                writeSet.dstSet = set;
                writeSet.dstBinding = 0;
                writeSet.dstArrayElement = 0;
                writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writeSet.descriptorCount = material::MAX_MATERIAL_TEXTURES;
                writeSet.pImageInfo = imageInfos.data();
                writes.push_back(writeSet);
            }

            device.getLogicalDevice().updateDescriptorSets(writes, nullptr);
        }
        textureDescriptorsInitialized = true;
    }

    void StaticMeshPipeline::createPipelineLayout()
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(MeshPushConstants);

        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            descriptorSetLayout,
            textureDescriptorSetLayout,
            parameterBufferCache->getDescriptorSetLayout()
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
        auto bindingDescription = MeshVertexInput::getBindingDescription();
        auto attributeDescriptions = MeshVertexInput::getAttributeDescriptions();

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

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

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Dynamic MSAA sample count — set per pass via cmd.setRasterizationSamplesEXT().
        vk::DynamicState meshDynStates[] = {vk::DynamicState::eRasterizationSamplesEXT};
        vk::PipelineDynamicStateCreateInfo meshDynamicState{};
        meshDynamicState.dynamicStateCount = 1;
        meshDynamicState.pDynamicStates = meshDynStates;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = vk::CompareOp::eLess;
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

        // Dynamic rendering: chain VkPipelineRenderingCreateInfo via pNext
        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::PipelineRenderingCreateInfo renderingCreateInfo{};
        renderingCreateInfo.colorAttachmentCount = 1;
        renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
        renderingCreateInfo.depthAttachmentFormat = swapChain.getSwapchainDepthStencilFormat();

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &renderingCreateInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(meshShader->getShaderStages().size());
        pipelineInfo.pStages = meshShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &meshDynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.subpass = 0;

        graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }
}
