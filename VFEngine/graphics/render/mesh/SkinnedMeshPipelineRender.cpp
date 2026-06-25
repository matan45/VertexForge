#include "SkinnedMeshPipeline.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <array>

namespace render::mesh
{
    bool SkinnedMeshPipeline::loadMeshFromFile(const std::string& meshPath)
    {
        if (meshPath.empty())
        {
            vfLogError("Empty mesh path");
            return false;
        }

        auto meshData = resource::MeshStreamResource::loadAll(meshPath);
        if (meshData.meshes.empty())
        {
            vfLogError("Failed to load mesh from: {}", meshPath);
            return false;
        }

        unloadMesh();

        loadedMesh = std::make_unique<SkinnedMeshGPUData>();
        loadedMesh->meshPath = meshPath;
        loadedMesh->hasSkinning = !meshData.skeleton.bones.empty();

        if (loadedMesh->hasSkinning)
        {
            loadedMesh->skeleton.boneNames.reserve(meshData.skeleton.bones.size());
            loadedMesh->skeleton.inverseBindPoses = meshData.skeleton.inverseBindPoses;
            for (const auto& bone : meshData.skeleton.bones)
            {
                loadedMesh->skeleton.boneNames.push_back(bone.name);
            }
        }

        createMeshGPUBuffers(meshData);

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        for (const auto& mesh : meshData.meshes)
        {
            if (!mesh.lodLevels.empty())
            {
                totalVertices += static_cast<uint32_t>(mesh.lodLevels[0].vertices.size());
                totalIndices += static_cast<uint32_t>(mesh.lodLevels[0].indices.size());
            }
        }

        vfLogInfo("Loaded mesh from file '{}': {} vertices, {} indices, {} bones",
                   meshPath, totalVertices, totalIndices, meshData.skeleton.bones.size());

        return true;
    }

    void SkinnedMeshPipeline::unloadMesh()
    {
        if (!loadedMesh) return;

        destroyMeshGPUBuffers();
        loadedMesh.reset();
    }

    void SkinnedMeshPipeline::loadMaterial(const ExtractedPBRValues& pbr)
    {
        // Lazily stand up a per-pipeline material texture cache. We only use its loader and
        // its 1x1-white default texture — the textures are written into THIS pipeline's own
        // set 1 (textureDescriptorSet), not into the cache's descriptor set.
        if (!materialTextureCache)
        {
            materialTextureCache = std::make_unique<MaterialTextureCache>(device);
            materialTextureCache->init(device.getStagingCommandPool());
        }

        // Slot order mirrors the static mesh shader / MaterialTexturePaths:
        //   0=albedo 1=normal 2=ORM 3=metallic 4=roughness 5=ao 6=emission 7=height.
        // The skinned shader samples 0..6 (height is unused in IBL lighting, kept for parity).
        struct SlotPath { const std::string& path; bool srgb; };
        const std::array<SlotPath, 8> slots = {{
            {pbr.albedoTexturePath,    true},   // 0
            {pbr.normalTexturePath,    false},  // 1
            {pbr.ormTexturePath,       false},  // 2
            {pbr.metallicTexturePath,  false},  // 3
            {pbr.roughnessTexturePath, false},  // 4
            {pbr.aoTexturePath,        false},  // 5
            {pbr.emissionTexturePath,  true},   // 6
            {pbr.heightTexturePath,    false},  // 7
        }};

        // Load each present texture; record per-slot index (slot number when bound, else NONE).
        std::array<uint8_t, 16> perSlotIndex;
        perSlotIndex.fill(SKINNED_TEXTURE_INDEX_NONE);
        for (uint8_t i = 0; i < static_cast<uint8_t>(slots.size()); ++i)
        {
            const auto& s = slots[i];
            if (s.path.empty()) continue;
            vk::Format fmt = s.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
            if (materialTextureCache->loadTexture(s.path, fmt))
            {
                perSlotIndex[i] = i;
            }
        }

        // Rewrite this pipeline's set 1: bound slots get their real view/sampler, every other
        // slot (and any failed load) stays on the default white texture so the array is fully
        // populated (UPDATE_AFTER_BIND is not used here — all 16 must be valid).
        std::array<vk::DescriptorImageInfo, SKINNED_MATERIAL_TEXTURE_SLOTS> imageInfos{};
        for (uint32_t i = 0; i < SKINNED_MATERIAL_TEXTURE_SLOTS; ++i)
        {
            const std::string& path = (i < slots.size() && perSlotIndex[i] != SKINNED_TEXTURE_INDEX_NONE)
                ? slots[i].path
                : std::string{};
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos[i].imageView = materialTextureCache->getViewForPath(path);
            imageInfos[i].sampler = materialTextureCache->getSamplerForPath(path);
        }

        vk::WriteDescriptorSet textureWrite{};
        textureWrite.dstSet = textureDescriptorSet;
        textureWrite.dstBinding = 0;
        textureWrite.dstArrayElement = 0;
        textureWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        textureWrite.descriptorCount = SKINNED_MATERIAL_TEXTURE_SLOTS;
        textureWrite.pImageInfo = imageInfos.data();

        device.getLogicalDevice().waitIdle(); // set may be in use by a prior frame's submit
        device.getLogicalDevice().updateDescriptorSets(textureWrite, nullptr);

        materialTextureIndices = {
            render::mesh::packTextureIndices(perSlotIndex[0], perSlotIndex[1], perSlotIndex[2], perSlotIndex[3]),
            render::mesh::packTextureIndices(perSlotIndex[4], perSlotIndex[5], perSlotIndex[6], perSlotIndex[7]),
            render::mesh::packTextureIndices(perSlotIndex[8], perSlotIndex[9], perSlotIndex[10], perSlotIndex[11]),
            render::mesh::packTextureIndices(perSlotIndex[12], perSlotIndex[13], perSlotIndex[14], perSlotIndex[15]),
        };
        materialTexturesActive = true;
    }

    void SkinnedMeshPipeline::clearMaterial()
    {
        materialTextureIndices = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        materialTexturesActive = false;
        // Leave textureDescriptorSet as-is: with all-NONE indices the shader never samples it.
    }

    void SkinnedMeshPipeline::createMeshGPUBuffers(const resource::MeshesData& meshesData)
    {
        if (!loadedMesh) return;

        bool boundingBoxInitialized = false;

        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.lodLevels.empty() || meshData.lodLevels[0].vertices.empty())
            {
                vfLogWarning("Skipping empty submesh in skinned mesh");
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            const auto& lod0 = meshData.lodLevels[0];
            bool subMeshBBInitialized = false;
            for (const auto& vertex : lod0.vertices)
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

                if (!boundingBoxInitialized)
                {
                    loadedMesh->meshData.boundingBox.min = vertex.position;
                    loadedMesh->meshData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    loadedMesh->meshData.boundingBox.expand(vertex.position);
                }
            }

            uint32_t lodCount = static_cast<uint32_t>(std::min(meshData.lodLevels.size(),
                                                               static_cast<size_t>(resource::LOD_LEVEL_COUNT)));

            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                const auto& srcLOD = meshData.lodLevels[lod];
                auto& dstLOD = subMesh.lodLevels[lod];

                if (srcLOD.vertices.empty())
                {
                    continue;
                }

                vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * srcLOD.vertices.size();
                core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                vertexBufferRequest.size = vertexBufferSize;
                vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                    vk::BufferUsageFlagBits::eTransferDst;
                vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(vertexBufferRequest, dstLOD.vertexBuffer,
                                                    dstLOD.vertexBufferAllocation, device.getMemoryManager());

                core::BufferUtilities::copyToBuffer(
                    device.getLogicalDevice(),
                    device.getPhysicalDevice(),
                    device.getGraphicsQueue(),
                    device.getStagingCommandPool(),
                    dstLOD.vertexBuffer,
                    srcLOD.vertices.data(),
                    vertexBufferSize
                );

                if (!srcLOD.indices.empty())
                {
                    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * srcLOD.indices.size();
                    core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                    indexBufferRequest.size = indexBufferSize;
                    indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                        vk::BufferUsageFlagBits::eTransferDst;
                    indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                    core::BufferUtilities::createBuffer(indexBufferRequest, dstLOD.indexBuffer,
                                                        dstLOD.indexBufferAllocation, device.getMemoryManager());

                    core::BufferUtilities::copyToBuffer(
                        device.getLogicalDevice(),
                        device.getPhysicalDevice(),
                        device.getGraphicsQueue(),
                        device.getStagingCommandPool(),
                        dstLOD.indexBuffer,
                        srcLOD.indices.data(),
                        indexBufferSize
                    );
                }

                dstLOD.vertexCount = static_cast<uint32_t>(srcLOD.vertices.size());
                dstLOD.indexCount = static_cast<uint32_t>(srcLOD.indices.size());
            }

            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const auto& srcLOD = meshData.lodLevels[lodCount - 1];
                auto& dstLOD = subMesh.lodLevels[lod];

                if (srcLOD.vertices.empty())
                {
                    continue;
                }

                vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * srcLOD.vertices.size();
                core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                vertexBufferRequest.size = vertexBufferSize;
                vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                    vk::BufferUsageFlagBits::eTransferDst;
                vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(vertexBufferRequest, dstLOD.vertexBuffer,
                                                    dstLOD.vertexBufferAllocation, device.getMemoryManager());

                core::BufferUtilities::copyToBuffer(
                    device.getLogicalDevice(),
                    device.getPhysicalDevice(),
                    device.getGraphicsQueue(),
                    device.getStagingCommandPool(),
                    dstLOD.vertexBuffer,
                    srcLOD.vertices.data(),
                    vertexBufferSize
                );

                if (!srcLOD.indices.empty())
                {
                    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * srcLOD.indices.size();
                    core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                    indexBufferRequest.size = indexBufferSize;
                    indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                        vk::BufferUsageFlagBits::eTransferDst;
                    indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                    core::BufferUtilities::createBuffer(indexBufferRequest, dstLOD.indexBuffer,
                                                        dstLOD.indexBufferAllocation, device.getMemoryManager());

                    core::BufferUtilities::copyToBuffer(
                        device.getLogicalDevice(),
                        device.getPhysicalDevice(),
                        device.getGraphicsQueue(),
                        device.getStagingCommandPool(),
                        dstLOD.indexBuffer,
                        srcLOD.indices.data(),
                        indexBufferSize
                    );
                }

                dstLOD.vertexCount = static_cast<uint32_t>(srcLOD.vertices.size());
                dstLOD.indexCount = static_cast<uint32_t>(srcLOD.indices.size());
            }

            loadedMesh->meshData.subMeshes.push_back(std::move(subMesh));
        }
    }

    void SkinnedMeshPipeline::destroyMeshGPUBuffers()
    {
        if (!loadedMesh) return;

        auto logicalDevice = device.getLogicalDevice();

        for (auto& subMesh : loadedMesh->meshData.subMeshes)
        {
            for (auto& lod : subMesh.lodLevels)
            {
                if (lod.vertexBuffer)
                {
                    logicalDevice.destroyBuffer(lod.vertexBuffer);
                    lod.vertexBuffer = nullptr;
                }
                device.getMemoryManager().free(lod.vertexBufferAllocation);
                lod.vertexBufferAllocation = {};
                if (lod.indexBuffer)
                {
                    logicalDevice.destroyBuffer(lod.indexBuffer);
                    lod.indexBuffer = nullptr;
                }
                device.getMemoryManager().free(lod.indexBufferAllocation);
                lod.indexBufferAllocation = {};
            }
        }
    }

    void SkinnedMeshPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                              const glm::vec3& cameraPos, float time) const
    {
        if (externalCameraBuffer) return;

        CameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        void* data = cameraUBOAllocation.mappedPtr;
        if (data)
        {
            memcpy(data, &ubo, sizeof(ubo));
        }
    }

    void SkinnedMeshPipeline::updateBoneMatrices(const std::vector<glm::mat4>& boneMatrices)
    {
        if (!boneSSBOMapped)
        {
            vfLogError("updateBoneMatrices: boneSSBOMapped is null!");
            return;
        }

        if (boneMatrices.size() > MAX_BONES)
        {
            static bool warnedOnce = false;
            if (!warnedOnce)
            {
                vfLogWarning("Bone count ({}) exceeds MAX_BONES ({}). Excess bones will be ignored. "
                              "Consider increasing MAX_BONES or simplifying the skeleton.",
                              boneMatrices.size(), MAX_BONES);
                warnedOnce = true;
            }
        }

        BoneMatricesSSBO* ssboData = static_cast<BoneMatricesSSBO*>(boneSSBOMapped);

        size_t boneCount = std::min(boneMatrices.size(), static_cast<size_t>(MAX_BONES));
        for (size_t i = 0; i < boneCount; ++i)
        {
            ssboData->boneMatrices[i] = boneMatrices[i];
        }
        ssboData->activeBoneCount = static_cast<uint32_t>(boneCount);
    }

    void SkinnedMeshPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                  uint32_t imageIndex,
                                                  const SkinnedMeshRenderData& renderData,
                                                  bool clearAttachments) const
    {
        if (!loadedMesh || loadedMesh->meshData.subMeshes.empty())
        {
            return;
        }

        auto colorAttach = clearAttachments
            ? core::colorClear(
                offscreenResources.colorImages[imageIndex].colorImageView,
                vk::ClearColorValue(std::array<float, 4>{
                    clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a}))
            : core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);
        auto depthAttach = clearAttachments
            ? core::depthClear(offscreenResources.depthImage.depthImageView, 1.0f, 0)
            : core::depthLoad(offscreenResources.depthImage.depthImageView);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = swapChain.getSwapchainExtent();
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);

        // VK-1433 Phase 3: bind the lazily-built wireframe (PolygonMode::eLine) variant when the
        // caller requests it; otherwise the original fill pipeline. The variant is built on demand
        // here (host op — safe to do while recording), then reused for subsequent frames.
        if (renderData.wireframe)
        {
            ensureWireframePipeline();
        }
        const bool useWireframe = renderData.wireframe && wireframePipeline;
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   useWireframe ? wireframePipeline : graphicsPipeline);

        std::array<vk::DescriptorSet, 3> descriptorSets = {
            cameraIBLDescriptorSet,
            textureDescriptorSet,
            boneDescriptorSet
        };
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                         0, descriptorSets, nullptr);

        SkinnedMeshPushConstants pc{};
        pc.model = renderData.modelMatrix;
        pc.albedo = renderData.albedo;
        pc.metallic = renderData.metallic;
        pc.roughness = renderData.roughness;
        pc.ao = renderData.ao;
        pc.emission = renderData.emission;
        // All-NONE by default (see SkinnedMeshRenderData) => shader samples no material
        // textures and the output matches the original scalar-only pipeline byte-for-byte.
        pc.textureIndicesPacked = renderData.textureIndicesPacked;
        // Phase 3 debug-shading + analytic-lighting tail. All default to the no-op state, so a
        // caller that leaves them untouched gets the original IBL-only output.
        pc.debugMode = renderData.debugMode;
        pc.lightingMode = renderData.lightingMode;
        pc.keyLightDirIntensity = glm::vec4(renderData.keyLightDirection, renderData.lightingIntensity);

        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(SkinnedMeshPushConstants), &pc);

        for (const auto& subMesh : loadedMesh->meshData.subMeshes)
        {
            const auto& lod = subMesh.lodLevels[0];

            if (!lod.isValid()) continue;

            vk::Buffer vertexBuffers[] = {lod.vertexBuffer};
            vk::DeviceSize offsets[] = {0};

            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
            commandBuffer.bindIndexBuffer(lod.indexBuffer, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(lod.indexCount, 1, 0, 0, 0);
            render::FrameDrawStats::count(render::DrawCategory::Meshes);
        }

        core::endDynamicRendering(commandBuffer);
    }
}
