#include "VFXMeshPreviewPipeline.hpp"
#include "../../mesh/MeshGPUCache.hpp"
#include "../compute/GPUVFXTypes.hpp" // VK-1526: MeshMaterialFlags
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/Texture.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace render::vfx
{
    void VFXMeshPreviewPipeline::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time) const
    {
        if (!cameraUBOMapped)
        {
            return;
        }

        VFXCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = time;

        std::memcpy(cameraUBOMapped, &ubo, sizeof(ubo));
    }

    void VFXMeshPreviewPipeline::setParticleInstances(const std::vector<VFXInstanceData>& instances)
    {
        if (instances.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(instances.size(),
                                                              static_cast<size_t>(maxInstances)));

        vk::DeviceSize bufferSize = sizeof(VFXInstanceData) * currentInstanceCount;
        std::memcpy(instanceBufferMapped, instances.data(), bufferSize);
    }

    void VFXMeshPreviewPipeline::setTexture(const std::string& texturePath)
    {
        if (texturePath == currentTexturePath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        customTexture.reset();
        currentTexturePath.clear();

        if (texturePath.empty() || !std::filesystem::exists(texturePath))
        {
            if (!texturePath.empty())
            {
                vfLogWarning("VFX mesh preview texture not found: {}", texturePath);
            }
            updateDescriptorSet();
            return;
        }

        try
        {
            customTexture = std::make_unique<core::Texture>(device);
            customTexture->loadTextureFromFile(texturePath, vk::Format::eR8G8B8A8Srgb, false);
            currentTexturePath = texturePath;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load VFX mesh preview texture '{}': {}", texturePath, e.what());
            customTexture.reset();
            currentTexturePath.clear();
        }

        updateDescriptorSet();
    }

    void VFXMeshPreviewPipeline::setMaterial(const ResolvedVFXMeshMaterial& resolved)
    {
        // Scalars/tint are cheap — always mirror them into the push constant.
        pushConstants.metallic = resolved.metallic;
        pushConstants.roughness = resolved.roughness;
        pushConstants.ao = resolved.ao;
        pushConstants.emissionStrength = resolved.emissionStrength;
        pushConstants.albedoTintR = resolved.albedoTint.r;
        pushConstants.albedoTintG = resolved.albedoTint.g;
        pushConstants.albedoTintB = resolved.albedoTint.b;
        pushConstants.albedoTintA = resolved.albedoTint.a;

        // Recompute the material flags from whichever maps are currently loaded (+ ORM eligibility).
        auto computeFlags = [&]() {
            uint32_t flags = MeshMaterialFlags::HasMaterial;
            if (matNormalTexture)                    flags |= MeshMaterialFlags::HasNormal;
            if (resolved.usesORM && matOrmTexture)   flags |= MeshMaterialFlags::UsesORM;
            if (matEmissiveTexture)                  flags |= MeshMaterialFlags::HasEmissive;
            return flags;
        };

        if (!resolved.hasMaterial)
        {
            pushConstants.materialFlags = 0; // legacy single-.vfImage path
            if (matAlbedoTexture || matNormalTexture || matOrmTexture || matEmissiveTexture || !currentMaterialKey.empty())
            {
                device.getLogicalDevice().waitIdle();
                matAlbedoTexture.reset();
                matNormalTexture.reset();
                matOrmTexture.reset();
                matEmissiveTexture.reset();
                currentMaterialKey.clear();
                updateDescriptorSet();
            }
            return;
        }

        const std::string ormPath = resolved.usesORM ? resolved.ormPath : std::string();
        const std::string key = resolved.albedoPath + "|" + resolved.normalPath + "|" +
                                ormPath + "|" + resolved.emissivePath;
        if (key == currentMaterialKey)
        {
            // Maps unchanged — only the scalars/flags may have moved (e.g. a .vfMatInstance override).
            pushConstants.materialFlags = computeFlags();
            return;
        }

        device.getLogicalDevice().waitIdle();

        auto loadMap = [&](std::unique_ptr<core::Texture>& tex, const std::string& path, vk::Format fmt) {
            tex.reset();
            if (path.empty() || !std::filesystem::exists(path))
            {
                if (!path.empty())
                {
                    vfLogWarning("VFX mesh preview material map not found: {}", path);
                }
                return;
            }
            try
            {
                tex = std::make_unique<core::Texture>(device);
                tex->loadTextureFromFile(path, fmt, false);
            }
            catch (const std::exception& e)
            {
                vfLogError("Failed to load VFX mesh preview material map '{}': {}", path, e.what());
                tex.reset();
            }
        };

        // sRGB for baseColor/emissive; linear (Unorm) for normal/ORM — matches the runtime bindless formats.
        loadMap(matAlbedoTexture, resolved.albedoPath, vk::Format::eR8G8B8A8Srgb);
        loadMap(matNormalTexture, resolved.normalPath, vk::Format::eR8G8B8A8Unorm);
        loadMap(matOrmTexture, ormPath, vk::Format::eR8G8B8A8Unorm);
        loadMap(matEmissiveTexture, resolved.emissivePath, vk::Format::eR8G8B8A8Srgb);

        currentMaterialKey = key;
        pushConstants.materialFlags = computeFlags();
        updateDescriptorSet();
    }

    void VFXMeshPreviewPipeline::setMesh(const std::string& meshPath)
    {
        if (meshPath == currentMeshPath)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        currentMeshPath = meshPath;
        meshVertexBuffer = nullptr;
        meshIndexBuffer = nullptr;
        meshIndexCount = 0;
        currentMeshId.clear();

        if (meshPath.empty())
        {
            return;
        }

        std::string meshId = meshCache.loadMesh(meshPath);
        if (meshId.empty())
        {
            vfLogWarning("VFXMeshPreviewPipeline: Failed to load mesh: {}", meshPath);
            return;
        }

        const auto* meshData = meshCache.getMesh(meshId);
        if (!meshData || meshData->subMeshes.empty())
        {
            vfLogWarning("VFXMeshPreviewPipeline: No submeshes in mesh: {}", meshPath);
            return;
        }

        const auto& lod0 = meshData->subMeshes[0].getLOD(0);
        if (!lod0.isValid())
        {
            vfLogWarning("VFXMeshPreviewPipeline: Invalid LOD 0 for mesh: {}", meshPath);
            return;
        }

        currentMeshId = meshId;
        meshVertexBuffer = lod0.vertexBuffer;
        meshIndexBuffer = lod0.indexBuffer;
        meshIndexCount = lod0.indexCount;

        vfLogInfo("VFXMeshPreviewPipeline: Mesh set: {} ({} indices)", meshPath, meshIndexCount);
    }

    void VFXMeshPreviewPipeline::setRenderingConfig(float alphaClipThreshold, ::vfx::VFXBlendMode blendMode,
                                                       const glm::vec3& glowColor,
                                                       float emissiveIntensity,
                                                       float uvScrollSpeedU, float uvScrollSpeedV)
    {
        pushConstants.alphaClipThreshold = alphaClipThreshold;
        pushConstants.blendMode = ::vfx::blendModeToGpuValue(blendMode);
        pushConstants.glowColorR = glowColor.r;
        pushConstants.glowColorG = glowColor.g;
        pushConstants.glowColorB = glowColor.b;
        pushConstants.emissiveIntensity = emissiveIntensity;
        pushConstants.uvScrollSpeedU = uvScrollSpeedU;
        pushConstants.uvScrollSpeedV = uvScrollSpeedV;
    }

    void VFXMeshPreviewPipeline::setOrientationConfig(uint32_t mode, const glm::vec3& axis, float spinRate)
    {
        pushConstants.meshOrientationMode = mode;
        pushConstants.orientAxisX = axis.x;
        pushConstants.orientAxisY = axis.y;
        pushConstants.orientAxisZ = axis.z;
        pushConstants.meshOrientationSpinRate = spinRate;
    }

    void VFXMeshPreviewPipeline::recordCommandBuffer(const vk::CommandBuffer& commandBuffer,
                                                       uint32_t imageIndex) const
    {
        if (!initialized)
        {
            return;
        }

        auto colorAttach = core::colorClear(
            offscreenResources.colorImages[imageIndex].colorImageView,
            vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f}));
        auto depthAttach = core::depthClear(
            offscreenResources.depthImage.depthImageView, 1.0f, 0);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = swapChain.getSwapchainExtent();
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);
        recordDraws(commandBuffer);
        core::endDynamicRendering(commandBuffer);
    }

    void VFXMeshPreviewPipeline::recordDraws(const vk::CommandBuffer& commandBuffer) const
    {
        if (!initialized || currentInstanceCount == 0 || meshIndexCount == 0 ||
            !meshVertexBuffer || !meshIndexBuffer)
        {
            return;
        }

        // VK-1472: Multiply blend selects the dedicated Multiply pipeline (shares the layout,
        // so the descriptor set + vertex/index bindings below stay valid); the other modes
        // share the premultiplied graphicsPipeline.
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
            (pushConstants.blendMode == 3u && multiplyPipeline) ? multiplyPipeline : graphicsPipeline);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, descriptorSet, nullptr);

        vk::Buffer vertexBuffers[] = {meshVertexBuffer, instanceBuffer};
        vk::DeviceSize offsets[] = {0, 0};
        commandBuffer.bindVertexBuffers(0, 2, vertexBuffers, offsets);

        commandBuffer.bindIndexBuffer(meshIndexBuffer, 0, vk::IndexType::eUint32);

        commandBuffer.pushConstants(pipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(VFXMeshPreviewPushConstants), &pushConstants);

        commandBuffer.drawIndexed(meshIndexCount, currentInstanceCount, 0, 0, 0);
    }
}
