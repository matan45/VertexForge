#include "MergedMeshBuffer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Log.hpp"
#include <material/MaterialInstanceTypes.hpp>
#include <glm/gtc/packing.hpp>
#include <cmath>

namespace render::gpudriven
{
    namespace
    {
        void resolveTextureIndices(GPUObjectData& obj, const std::string& materialPath,
                                   const TextureIndexResolver& textureResolver)
        {
            if (textureResolver && !materialPath.empty())
            {
                obj.textureIndices0 = glm::uvec4(
                    textureResolver(materialPath, TextureSlotType::Albedo),
                    textureResolver(materialPath, TextureSlotType::Normal),
                    textureResolver(materialPath, TextureSlotType::ORM),
                    textureResolver(materialPath, TextureSlotType::Metallic)
                );
                obj.textureIndices1 = glm::uvec4(
                    textureResolver(materialPath, TextureSlotType::Roughness),
                    textureResolver(materialPath, TextureSlotType::AO),
                    textureResolver(materialPath, TextureSlotType::Emission),
                    textureResolver(materialPath, TextureSlotType::Height)
                );
            }
            else
            {
                obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
                obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
            }
        }
    }

    void MergedMeshBuffer::populateLODData(GPUObjectData& obj,
                                           const mesh::MeshRenderData& meshRender,
                                           const SubmeshLocation& submeshLoc,
                                           const BoneOffsetResolver& boneOffsetResolver)
    {
        for (uint32_t i = 0; i < LOD_LEVEL_COUNT; ++i)
        {
            glm::uvec4& lodData = (i == 0) ? obj.lod0Data
                                  : (i == 1) ? obj.lod1Data
                                  : (i == 2) ? obj.lod2Data
                                  : obj.lod3Data;
            lodData = glm::uvec4(
                submeshLoc.lods[i].vertexOffset,
                submeshLoc.lods[i].indexOffset,
                submeshLoc.lods[i].indexCount,
                submeshLoc.lods[i].vertexCount
            );
        }

        obj.meshletLod0 = glm::uvec4(
            submeshLoc.meshletLods[0].meshletOffset,
            submeshLoc.meshletLods[0].meshletCount,
            submeshLoc.meshletLods[0].baseVertexOffset, 0);
        obj.meshletLod1 = glm::uvec4(
            submeshLoc.meshletLods[1].meshletOffset,
            submeshLoc.meshletLods[1].meshletCount,
            submeshLoc.meshletLods[1].baseVertexOffset, 0);
        obj.meshletLod2 = glm::uvec4(
            submeshLoc.meshletLods[2].meshletOffset,
            submeshLoc.meshletLods[2].meshletCount,
            submeshLoc.meshletLods[2].baseVertexOffset, 0);

        uint32_t boneOffset = INVALID_BONE_OFFSET;
        if (boneOffsetResolver && meshRender.entity != entt::null)
            boneOffset = boneOffsetResolver(meshRender.entity);

        obj.meshletLod3 = glm::uvec4(
            submeshLoc.meshletLods[3].meshletOffset,
            submeshLoc.meshletLods[3].meshletCount,
            submeshLoc.meshletLods[3].baseVertexOffset,
            boneOffset);

        float bias = meshRender.lodBias;
        obj.lodThresholds = glm::vec4(
            LOD_THRESHOLD_0 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_1 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_2 * std::pow(2.0f, -bias),
            bias);
    }

    std::string MergedMeshBuffer::resolveMaterialProperties(GPUObjectData& obj,
                                                             const mesh::MeshRenderData& meshRender,
                                                             const SubmeshLocation& submeshLoc)
    {
        const auto* subMat = meshRender.getMaterialForSubmesh(submeshLoc.submeshName);
        std::string materialPath;

        if (subMat)
        {
            obj.albedo = subMat->albedo;
            obj.iblParams = glm::vec4(subMat->iblDiffuse, subMat->iblSpecular, subMat->alphaCutoff, 0.0f);
            obj.materialParams = glm::vec4(subMat->metallic, subMat->roughness, subMat->ao, subMat->emission);
            materialPath = subMat->materialPath;
        }
        else
        {
            obj.albedo = meshRender.albedo;
            materialPath = meshRender.defaultMaterialPath;

            float iblDiffuse = 1.0f, iblSpecular = 0.5f, alphaCutoff = 0.5f;
            if (!materialPath.empty())
            {
                auto it = pbrCache.find(materialPath);
                if (it == pbrCache.end())
                {
                    it = pbrCache.emplace(materialPath,
                                          mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath)).first;
                }
                iblDiffuse = it->second.iblDiffuse;
                iblSpecular = it->second.iblSpecular;
                alphaCutoff = it->second.alphaCutoff;
            }
            obj.iblParams = glm::vec4(iblDiffuse, iblSpecular, alphaCutoff, 0.0f);
            obj.materialParams = glm::vec4(meshRender.metallic, meshRender.roughness, meshRender.ao,
                                           meshRender.emission);
        }

        return materialPath;
    }

    void MergedMeshBuffer::applyDynamicEmission(GPUObjectData& obj, const std::string& materialPath, float time)
    {
        std::string effectiveMaterialPath = materialPath;
        if (material::isInstanceFile(materialPath))
        {
            auto cacheIt = instanceToParentCache.find(materialPath);
            if (cacheIt != instanceToParentCache.end())
            {
                effectiveMaterialPath = cacheIt->second;
            }
            else
            {
                auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
                if (instanceData && !instanceData->parentMaterialPath.empty())
                {
                    effectiveMaterialPath = instanceData->parentMaterialPath;
                    instanceToParentCache[materialPath] = effectiveMaterialPath;
                }
            }
        }

        auto matData = resource::ResourceManager::loadMaterial(effectiveMaterialPath);
        if (matData)
        {
            const auto* outputNode = matData->graph.findOutputNode();
            if (outputNode)
            {
                float dynamicEmission = mesh::MaterialPBRExtractor::evaluateEmissionStrength(
                    matData->graph, outputNode->id, time);
                if (dynamicEmission != 0.0f)
                    obj.materialParams.w = dynamicEmission;
            }
        }
    }

    void MergedMeshBuffer::populateObjectData(GPUObjectData& obj,
                                              const mesh::MeshRenderData& meshRender,
                                              const SubmeshLocation& submeshLoc,
                                              const ObjectResolvers& resolvers)
    {
        obj.modelMatrix = meshRender.modelMatrix;
        float drawDistSq = meshRender.maxDrawDistance > 0.0f
            ? meshRender.maxDrawDistance * meshRender.maxDrawDistance : 0.0f;
        obj.aabbMin = glm::vec4(submeshLoc.aabbMin, drawDistSq);
        obj.aabbMax = glm::vec4(submeshLoc.aabbMax, 0.0f);

        populateLODData(obj, meshRender, submeshLoc, resolvers.boneOffsetResolver);

        std::string materialPath = resolveMaterialProperties(obj, meshRender, submeshLoc);

        if (!materialPath.empty() && resolvers.time > 0.0f)
            applyDynamicEmission(obj, materialPath, resolvers.time);

        resolveTextureIndices(obj, materialPath, resolvers.textureResolver);

        const auto* subMat = meshRender.getMaterialForSubmesh(submeshLoc.submeshName);
        obj.flags = 0;
        if (subMat)
        {
            switch (subMat->blendMode)
            {
            case 1: obj.flags |= ObjectFlags::AlphaMask; break;
            case 2: obj.flags |= ObjectFlags::Translucent; break;
            case 3: obj.flags |= ObjectFlags::Translucent | ObjectFlags::AdditiveBlend; break;
            case 4: obj.flags |= ObjectFlags::Translucent | ObjectFlags::MultiplyBlend; break;
            default: break;
            }

            if (subMat->blendMode >= 2)
            {
                obj.albedo.a = subMat->opacity;
            }
        }
        else if (!materialPath.empty())
        {
            auto it = pbrCache.find(materialPath);
            if (it != pbrCache.end())
            {
                switch (static_cast<uint8_t>(it->second.blendMode))
                {
                case 1: obj.flags |= ObjectFlags::AlphaMask; break;
                case 2: obj.flags |= ObjectFlags::Translucent; break;
                case 3: obj.flags |= ObjectFlags::Translucent | ObjectFlags::AdditiveBlend; break;
                case 4: obj.flags |= ObjectFlags::Translucent | ObjectFlags::MultiplyBlend; break;
                default: break;
                }

                if (material::isTransparentBlendMode(it->second.blendMode))
                {
                    obj.albedo.a = it->second.opacity;
                }
            }
        }

        float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
        float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
        float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
        constexpr float uniformScaleEpsilon = 0.001f;
        if (std::abs(scaleX - scaleY) < uniformScaleEpsilon &&
            std::abs(scaleY - scaleZ) < uniformScaleEpsilon)
        {
            obj.flags |= ObjectFlags::UniformScale;
        }

        obj.availableLODMask = submeshLoc.getAvailableLODMask();
        obj.shaderGroupIndex = (resolvers.shaderGroupResolver && !materialPath.empty())
                                   ? resolvers.shaderGroupResolver(materialPath) : 0;

        uint32_t lightmapIdx = INVALID_TEXTURE_INDEX;
        if (!meshRender.lightmapPath.empty() && resolvers.lightmapResolver)
        {
            lightmapIdx = resolvers.lightmapResolver(meshRender.lightmapPath);
        }

        if (lightmapIdx != INVALID_TEXTURE_INDEX)
        {
            obj.lightmapData = glm::uvec4(
                lightmapIdx,
                glm::packHalf2x16(glm::vec2(meshRender.lightmapScaleOffset.x, meshRender.lightmapScaleOffset.y)),
                glm::packHalf2x16(glm::vec2(meshRender.lightmapScaleOffset.z, meshRender.lightmapScaleOffset.w)),
                0
            );
        }
        else
        {
            obj.lightmapData = glm::uvec4(INVALID_TEXTURE_INDEX, 0, 0, 0);
        }
    }

    void MergedMeshBuffer::updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                                         const ObjectResolvers& resolvers)
    {
        currentObjectCount = 0;
        transparentObjectCount = 0;

        for (const auto& meshRender : renderData)
        {
            auto meshIt = meshPathToIndex.find(meshRender.meshPath);
            if (meshIt == meshPathToIndex.end())
            {
                continue;
            }

            const auto& meshInfo = registeredMeshes[meshIt->second];

            for (uint32_t subIdx = 0; subIdx < meshInfo.submeshCount; ++subIdx)
            {
                const auto& submeshLoc = allSubmeshLocations[meshInfo.firstSubmeshIndex + subIdx];

                if (!submeshLoc.hasRenderableLOD())
                {
                    continue;
                }

                if (currentObjectCount >= maxObjectCount)
                {
                    vfLogWarning("MergedMeshBuffer: max object count reached");
                    return;
                }

                GPUObjectData& obj = cpuObjectData[currentObjectCount];
                populateObjectData(obj, meshRender, submeshLoc, resolvers);
                obj.entityId = currentObjectCount;

                if (obj.shaderGroupIndex == SHADER_GROUP_TRANSPARENT)
                {
                    transparentObjectCount++;
                }

                currentObjectCount++;
            }
        }
    }

}
