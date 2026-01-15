#include "Mesh.hpp"
#include "print/EditorLogger.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cfloat>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>

// V-HACD for convex decomposition (header-only, implementation in this TU)
#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>

namespace types
{
    ExtractedSkeleton Mesh::extractSkeleton(const aiScene* scene) const
    {
        ExtractedSkeleton skeleton;

        // Collect all bones from all meshes
        for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
        {
            const aiMesh* mesh = scene->mMeshes[m];
            if (!mesh->HasBones())
                continue;

            skeleton.hasSkinning = true;

            for (unsigned int b = 0; b < mesh->mNumBones; ++b)
            {
                const aiBone* bone = mesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                // Skip if we already have this bone
                if (skeleton.boneNameToIndex.contains(boneName))
                    continue;

                uint32_t boneIndex = static_cast<uint32_t>(skeleton.boneNames.size());
                skeleton.boneNameToIndex[boneName] = boneIndex;
                skeleton.boneNames.push_back(boneName);

                // Convert Assimp matrix (row-major) to GLM matrix (column-major)
                const auto& aiMat = bone->mOffsetMatrix;
                glm::mat4 offsetMatrix = glm::transpose(glm::mat4(
                    aiMat.a1, aiMat.a2, aiMat.a3, aiMat.a4,
                    aiMat.b1, aiMat.b2, aiMat.b3, aiMat.b4,
                    aiMat.c1, aiMat.c2, aiMat.c3, aiMat.c4,
                    aiMat.d1, aiMat.d2, aiMat.d3, aiMat.d4
                ));
                skeleton.inverseBindPoses.push_back(offsetMatrix);
            }
        }

        if (skeleton.hasSkinning)
        {
            vfLogInfo("Extracted skeleton with {} bones", skeleton.boneNames.size());
        }

        return skeleton;
    }

    void Mesh::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, MeshProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(file.path.data(),
                                                 aiProcess_Triangulate | aiProcess_FlipUVs |
                                                 aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            vfLogError("Failed to load Mesh file: {}", importer.GetErrorString());
            return;
        }

        if (progressCallback) progressCallback(0.2f);

        saveToFileStreamingWithLOD(location, fileName, scene, file.config, progressCallback);

        if (progressCallback) progressCallback(1.0f);
    }

    LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh) const
    {
        // Call the new overload with empty skeleton
        ExtractedSkeleton emptySkeleton;
        return convertAssimpMesh(assimpMesh, emptySkeleton);
    }

    LODMeshData Mesh::convertAssimpMesh(const aiMesh* assimpMesh, const ExtractedSkeleton& skeleton) const
    {
        LODMeshData result;
        result.vertices.reserve(assimpMesh->mNumVertices);

        // Initialize vertices with basic data
        for (unsigned int v = 0; v < assimpMesh->mNumVertices; ++v)
        {
            resource::Vertex vertex;
            vertex.position = {
                assimpMesh->mVertices[v].x,
                assimpMesh->mVertices[v].y,
                assimpMesh->mVertices[v].z
            };

            if (assimpMesh->HasNormals())
            {
                vertex.normal = {
                    assimpMesh->mNormals[v].x,
                    assimpMesh->mNormals[v].y,
                    assimpMesh->mNormals[v].z
                };
            }
            else
            {
                vertex.normal = {0.0f, 0.0f, 0.0f};
            }

            if (assimpMesh->mTextureCoords[0])
            {
                vertex.texCoords = {
                    assimpMesh->mTextureCoords[0][v].x,
                    assimpMesh->mTextureCoords[0][v].y
                };
            }
            else
            {
                vertex.texCoords = {0.0f, 0.0f};
            }

            // Initialize bone data to defaults (no bone influence)
            vertex.boneIndices = glm::ivec4(-1, -1, -1, -1);
            vertex.boneWeights = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

            result.vertices.push_back(vertex);
        }

        // Extract bone weights if mesh has bones and skeleton is available
        if (assimpMesh->HasBones() && skeleton.hasSkinning)
        {
            // Track how many bones we've assigned to each vertex
            std::vector<uint32_t> vertexBoneCount(assimpMesh->mNumVertices, 0);

            for (unsigned int b = 0; b < assimpMesh->mNumBones; ++b)
            {
                const aiBone* bone = assimpMesh->mBones[b];
                std::string boneName = bone->mName.C_Str();

                auto it = skeleton.boneNameToIndex.find(boneName);
                if (it == skeleton.boneNameToIndex.end())
                {
                    vfLogWarning("Bone '{}' not found in skeleton", boneName);
                    continue;
                }

                int32_t boneIndex = static_cast<int32_t>(it->second);

                for (unsigned int w = 0; w < bone->mNumWeights; ++w)
                {
                    uint32_t vertexId = bone->mWeights[w].mVertexId;
                    float weight = bone->mWeights[w].mWeight;

                    if (vertexId >= result.vertices.size())
                        continue;

                    uint32_t& boneSlot = vertexBoneCount[vertexId];
                    if (boneSlot < MAX_BONES_PER_VERTEX)
                    {
                        result.vertices[vertexId].boneIndices[boneSlot] = boneIndex;
                        result.vertices[vertexId].boneWeights[boneSlot] = weight;
                        ++boneSlot;
                    }
                }
            }

            // Normalize bone weights for each vertex
            size_t verticesWithBones = 0;
            for (auto& vertex : result.vertices)
            {
                float totalWeight = vertex.boneWeights.x + vertex.boneWeights.y +
                                    vertex.boneWeights.z + vertex.boneWeights.w;
                if (totalWeight > 0.0f)
                {
                    vertex.boneWeights /= totalWeight;
                    ++verticesWithBones;
                }
            }

            vfLogInfo("Mesh has {} vertices with bone weights out of {} total",
                      verticesWithBones, result.vertices.size());

            // Debug: Check bone index distribution
            std::unordered_map<int32_t, size_t> boneIndexCounts;
            for (const auto& vertex : result.vertices)
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (vertex.boneIndices[i] >= 0)
                    {
                        boneIndexCounts[vertex.boneIndices[i]]++;
                    }
                }
            }
            vfLogInfo("Bone index distribution: {} unique bone indices used",
                      boneIndexCounts.size());
        }

        // Extract indices
        uint32_t totalIndices = 0;
        for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f)
        {
            totalIndices += assimpMesh->mFaces[f].mNumIndices;
        }
        result.indices.reserve(totalIndices);

        for (unsigned int f = 0; f < assimpMesh->mNumFaces; ++f)
        {
            const aiFace& face = assimpMesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k)
            {
                result.indices.push_back(face.mIndices[k]);
            }
        }

        return result;
    }

    LODMeshData Mesh::simplifyMesh(const LODMeshData& source, float targetRatio) const
    {
        if (source.indices.empty() || source.vertices.empty())
        {
            return source;
        }

        if (targetRatio >= 1.0f)
        {
            return source;
        }

        size_t targetIndexCount = static_cast<size_t>(source.indices.size() * targetRatio);
        // Ensure at least 3 indices (one triangle)
        targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
        // Round to multiple of 3
        targetIndexCount = (targetIndexCount / 3) * 3;

        LODMeshData result;
        result.indices.resize(source.indices.size()); // Allocate max size initially

        size_t actualIndexCount = meshopt_simplifySloppy(
            result.indices.data(),
            source.indices.data(),
            source.indices.size(),
            reinterpret_cast<const float*>(source.vertices.data()),
            source.vertices.size(),
            sizeof(resource::Vertex),
            targetIndexCount,
            FLT_MAX, // No error limit - allow maximum simplification
            nullptr // No result error output needed
        );

        result.indices.resize(actualIndexCount);

        if (actualIndexCount == source.indices.size())
        {
            vfLogWarning("  Simplification failed for ratio {:.1f}%, keeping original", targetRatio * 100.0f);
            return source;
        }

        // Optimize vertex cache for better GPU performance
        meshopt_optimizeVertexCache(
            result.indices.data(),
            result.indices.data(),
            result.indices.size(),
            source.vertices.size()
        );

        std::vector<unsigned int> remap(source.vertices.size(), ~0u);
        size_t uniqueVertexCount = 0;

        for (size_t i = 0; i < result.indices.size(); ++i)
        {
            uint32_t idx = result.indices[i];
            if (remap[idx] == ~0u)
            {
                remap[idx] = static_cast<unsigned int>(uniqueVertexCount++);
            }
        }

        result.vertices.resize(uniqueVertexCount);
        for (size_t i = 0; i < source.vertices.size(); ++i)
        {
            if (remap[i] != ~0u)
            {
                result.vertices[remap[i]] = source.vertices[i];
            }
        }

        for (size_t i = 0; i < result.indices.size(); ++i)
        {
            result.indices[i] = remap[result.indices[i]];
        }

        return result;
    }

    std::array<LODMeshData, resource::LOD_LEVEL_COUNT> Mesh::generateLODLevels(const LODMeshData& lod0) const
    {
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lodLevels;

        // LOD0: Original mesh (100%)
        lodLevels[0] = lod0;

        // Generate LOD1, LOD2, LOD3 by simplifying from LOD0
        for (uint32_t level = 1; level < resource::LOD_LEVEL_COUNT; ++level)
        {
            lodLevels[level] = simplifyMesh(lod0, lodRatios[level]);

            vfLogInfo("  LOD{}: {} vertices, {} triangles ({}%)",
                      level,
                      lodLevels[level].vertices.size(),
                      lodLevels[level].indices.size() / 3,
                      static_cast<int>(lodRatios[level] * 100));
        }

        return lodLevels;
    }

    MeshletBuildResult Mesh::buildMeshletsForLOD(const LODMeshData& lodMesh) const
    {
        MeshletBuildResult result;

        if (lodMesh.indices.empty() || lodMesh.vertices.empty())
        {
            return result;
        }

        // Calculate maximum number of meshlets needed
        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodMesh.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshoptMeshlets.data(),
            meshletVertexIndices.data(),
            meshletTriangleIndices.data(),
            lodMesh.indices.data(),
            lodMesh.indices.size(),
            reinterpret_cast<const float*>(lodMesh.vertices.data()),
            lodMesh.vertices.size(),
            sizeof(resource::Vertex),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES,
            0.0f // cone_weight: 0 = balanced locality
        );

        if (meshletCount == 0)
        {
            return result;
        }

        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3);

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        // Convert to our format and compute bounds
        result.meshlets.resize(meshletCount);
        result.meshletVertices.resize(totalVertexIndices);
        result.meshletPrimitives.reserve((totalTriangleIndices + 3) / 4);

        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            result.meshletVertices[i] = meshletVertexIndices[i];
        }

        // Pack triangle indices (3 uint8 per triangle -> 1 uint32 per triangle)
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;
                uint32_t packed =
                    static_cast<uint32_t>(meshletTriangleIndices[triOffset + 0]) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 1]) << 8) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 2]) << 16);
                result.meshletPrimitives.push_back(packed);
            }
        }

        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = result.meshlets[i];

            outMeshlet.descriptor.vertexOffset = m.vertex_offset;
            outMeshlet.descriptor.primitiveOffset = primitiveOffset;
            outMeshlet.descriptor.vertexCount = static_cast<uint8_t>(m.vertex_count);
            outMeshlet.descriptor.primitiveCount = static_cast<uint8_t>(m.triangle_count);
            outMeshlet.descriptor.padding = 0;

            primitiveOffset += m.triangle_count;

            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertexIndices[m.vertex_offset],
                &meshletTriangleIndices[m.triangle_offset],
                m.triangle_count,
                reinterpret_cast<const float*>(lodMesh.vertices.data()),
                lodMesh.vertices.size(),
                sizeof(resource::Vertex)
            );

            outMeshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius
            );

            // Cone for backface culling (cutoff >= 1.0 means no backface culling)
            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );
        }

        vfLogInfo("    Generated {} meshlets ({} vertex indices, {} primitives)",
                  meshletCount, totalVertexIndices, result.meshletPrimitives.size());

        return result;
    }

    void Mesh::writeMeshletData(std::ofstream& outFile,
                                const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const
    {
        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            const auto& result = meshletResults[lod];
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshlets.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletVertices.size()));
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(result.meshletPrimitives.size()));
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (const auto& meshlet : meshletResults[lod].meshlets)
            {
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.vertexOffset);
                resource::endian::writeLE<uint32_t>(outFile, meshlet.descriptor.primitiveOffset);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.vertexCount);
                resource::endian::writeLE<uint8_t>(outFile, meshlet.descriptor.primitiveCount);
                resource::endian::writeLE<uint16_t>(outFile, 0); // padding

                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.boundingSphere.w);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.x);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.y);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.z);
                resource::endian::writeLE<float>(outFile, meshlet.bounds.cone.w);
            }
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t idx : meshletResults[lod].meshletVertices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }
        }

        for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
        {
            for (uint32_t packed : meshletResults[lod].meshletPrimitives)
            {
                resource::endian::writeLE<uint32_t>(outFile, packed);
            }
        }
    }

    void Mesh::writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const
    {
        constexpr size_t verticesPerChunk = chunkSize / sizeof(resource::Vertex);

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.vertices.size()));

        for (size_t v = 0; v < lodMesh.vertices.size(); v += verticesPerChunk)
        {
            size_t chunkEnd = std::min(v + verticesPerChunk, lodMesh.vertices.size());

            for (size_t j = v; j < chunkEnd; ++j)
            {
                const auto& vertex = lodMesh.vertices[j];
                // Position
                resource::endian::writeLE<float>(outFile, vertex.position.x);
                resource::endian::writeLE<float>(outFile, vertex.position.y);
                resource::endian::writeLE<float>(outFile, vertex.position.z);
                // Normal
                resource::endian::writeLE<float>(outFile, vertex.normal.x);
                resource::endian::writeLE<float>(outFile, vertex.normal.y);
                resource::endian::writeLE<float>(outFile, vertex.normal.z);
                // TexCoords
                resource::endian::writeLE<float>(outFile, vertex.texCoords.x);
                resource::endian::writeLE<float>(outFile, vertex.texCoords.y);
                // Bone indices (4 int32)
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.x);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.y);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.z);
                resource::endian::writeLE<int32_t>(outFile, vertex.boneIndices.w);
                // Bone weights (4 float)
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.x);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.y);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.z);
                resource::endian::writeLE<float>(outFile, vertex.boneWeights.w);
            }
        }

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(lodMesh.indices.size()));

        constexpr size_t indicesPerChunk = chunkSize / sizeof(uint32_t);
        for (size_t i = 0; i < lodMesh.indices.size(); i += indicesPerChunk)
        {
            size_t chunkEnd = std::min(i + indicesPerChunk, lodMesh.indices.size());
            std::vector<uint32_t> indexChunk(lodMesh.indices.begin() + i, lodMesh.indices.begin() + chunkEnd);
            resource::endian::writeVectorLE<uint32_t>(outFile, indexChunk);
        }
    }

    void Mesh::saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                          const aiScene* scene, const importConfig::ImportConfig& config,
                                          MeshProgressCallback progressCallback) const
    {
        std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
            FileExtension::mesh);
        std::ofstream outFile(newFileLocation, std::ios::binary);

        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", newFileLocation.string());
            return;
        }

        // Extract skeleton from all meshes first (for vertex bone indices)
        ExtractedSkeleton skeleton = extractSkeleton(scene);

        // Write header with version 0.0.7 (LOD + Meshlet + Convex + Skeleton reference)
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::MESH));
        resource::endian::writeLE<uint32_t>(outFile, 0); // major
        resource::endian::writeLE<uint32_t>(outFile, 0); // minor
        resource::endian::writeLE<uint32_t>(outFile, 7); // patch - version 0.0.7 for skeleton reference
        resource::endian::writeLE<uint32_t>(outFile, scene->mNumMeshes);

        // Write empty skeleton reference (self-contained animation format - no external skeleton)
        resource::endian::writeLE<uint32_t>(outFile, 0);

        vfLogInfo("Generating LODs and meshlets for {} submeshes...", scene->mNumMeshes);

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh* assimpMesh = scene->mMeshes[i];
            std::string meshName = assimpMesh->mName.C_Str();

            vfLogInfo("Processing submesh '{}' ({} vertices, {} triangles)...",
                      meshName,
                      assimpMesh->mNumVertices,
                      assimpMesh->mNumFaces);

            uint32_t nameLength = static_cast<uint32_t>(meshName.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(meshName.data(), nameLength);
            }

            resource::endian::writeLE<uint32_t>(outFile, resource::LOD_LEVEL_COUNT);

            // Convert mesh with bone data extraction
            LODMeshData lod0 = convertAssimpMesh(assimpMesh, skeleton);
            auto lodLevels = generateLODLevels(lod0);

            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                writeLODLevel(outFile, lodLevels[lod]);
            }

            vfLogInfo("  Generating meshlets...");
            std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT> meshletResults;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                meshletResults[lod] = buildMeshletsForLOD(lodLevels[lod]);
            }

            writeMeshletData(outFile, meshletResults);

            // Use LOD0 for best accuracy in convex decomposition
            resource::ConvexDecompositionData convexData = generateConvexDecomposition(
                lod0, config.meshConfig);
            writeConvexDecompositionData(outFile, convexData);

            if (progressCallback)
            {
                float progress = 0.2f + (static_cast<float>(i + 1) / scene->mNumMeshes) * 0.75f;
                progressCallback(progress);
            }
        }

        // Write skeleton data at the end of the file
        writeSkeletonData(outFile, skeleton);

        outFile.close();
        vfLogInfo("Mesh with LOD, meshlets and skeleton reference saved to: {}", newFileLocation.string());
    }

    resource::ConvexDecompositionData Mesh::generateConvexDecomposition(
        const LODMeshData& meshData,
        const importConfig::MeshImportConfig& config) const
    {
        resource::ConvexDecompositionData result;

        if (!config.generateConvexDecomposition || meshData.vertices.empty() || meshData.indices.empty())
        {
            return result;
        }

        vfLogInfo("  Running V-HACD convex decomposition...");

        std::vector<double> points;
        points.reserve(meshData.vertices.size() * 3);
        for (const auto& v : meshData.vertices)
        {
            points.push_back(static_cast<double>(v.position.x));
            points.push_back(static_cast<double>(v.position.y));
            points.push_back(static_cast<double>(v.position.z));
        }

        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = config.maxConvexHulls;
        params.m_resolution = config.vhacdResolution;
        params.m_maxNumVerticesPerCH = config.maxVerticesPerHull;
        params.m_minimumVolumePercentErrorAllowed = static_cast<double>(config.minVolumePercentError);
        params.m_maxRecursionDepth = 10;
        params.m_shrinkWrap = true;
        params.m_asyncACD = false; // Synchronous for import pipeline

        VHACD::IVHACD* vhacd = VHACD::CreateVHACD();

        bool success = vhacd->Compute(
            points.data(),
            static_cast<uint32_t>(meshData.vertices.size()),
            meshData.indices.data(),
            static_cast<uint32_t>(meshData.indices.size() / 3),
            params
        );

        if (success)
        {
            uint32_t numHulls = vhacd->GetNConvexHulls();
            vfLogInfo("  V-HACD generated {} convex hulls", numHulls);

            result.hasDecomposition = true;
            result.params.maxConvexHulls = config.maxConvexHulls;
            result.params.resolution = config.vhacdResolution;
            result.params.maxVerticesPerHull = config.maxVerticesPerHull;
            result.params.minVolumePercentError = config.minVolumePercentError;

            // Jolt Physics hard limit for convex hull vertices
            constexpr uint32_t joltMaxVertices = 256;
            const uint32_t effectiveMaxVertices = std::min(config.maxVerticesPerHull, joltMaxVertices);

            uint32_t skippedHulls = 0;

            for (uint32_t i = 0; i < numHulls; ++i)
            {
                VHACD::IVHACD::ConvexHull hull;
                vhacd->GetConvexHull(i, hull);

                // Validate hull vertex count against Jolt's limit
                if (hull.m_points.size() > effectiveMaxVertices)
                {
                    vfLogWarning("  Hull {} has {} vertices (exceeds limit of {}), skipping",
                                 i, hull.m_points.size(), effectiveMaxVertices);
                    ++skippedHulls;
                    continue;
                }

                if (hull.m_points.empty())
                {
                    ++skippedHulls;
                    continue;
                }

                result.hulls.emplace_back();
                auto& outHull = result.hulls.back();
                outHull.vertices.reserve(hull.m_points.size());

                for (const auto& p : hull.m_points)
                {
                    outHull.vertices.emplace_back(
                        static_cast<float>(p.mX),
                        static_cast<float>(p.mY),
                        static_cast<float>(p.mZ)
                    );
                }

                outHull.indices.reserve(hull.m_triangles.size() * 3);
                for (const auto& tri : hull.m_triangles)
                {
                    outHull.indices.push_back(tri.mI0);
                    outHull.indices.push_back(tri.mI1);
                    outHull.indices.push_back(tri.mI2);
                }

                outHull.center = glm::vec3(
                    static_cast<float>(hull.m_center.GetX()),
                    static_cast<float>(hull.m_center.GetY()),
                    static_cast<float>(hull.m_center.GetZ())
                );
                outHull.volume = static_cast<float>(hull.m_volume);
            }

            if (skippedHulls > 0)
            {
                vfLogWarning("  Skipped {} hulls due to vertex count limits", skippedHulls);
            }

            if (result.hulls.empty())
            {
                vfLogWarning("  All hulls were skipped, decomposition invalid");
                result.hasDecomposition = false;
            }
            else
            {
                vfLogInfo("  Final hull count: {}, total vertices: {}",
                          result.hulls.size(), result.getTotalVertexCount());
            }
        }
        else
        {
            vfLogWarning("  V-HACD decomposition failed");
        }

        vhacd->Release();
        return result;
    }

    void Mesh::writeConvexDecompositionData(std::ofstream& outFile,
                                            const resource::ConvexDecompositionData& decomposition) const
    {
        resource::endian::writeLE<uint8_t>(outFile, decomposition.hasDecomposition ? 1 : 0);

        if (!decomposition.hasDecomposition)
        {
            return;
        }

        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxConvexHulls);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.resolution);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxVerticesPerHull);
        resource::endian::writeLE<float>(outFile, decomposition.params.minVolumePercentError);
        resource::endian::writeLE<uint32_t>(outFile, decomposition.params.maxRecursionDepth);

        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(decomposition.hulls.size()));

        for (const auto& hull : decomposition.hulls)
        {
            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.vertices.size()));
            for (const auto& v : hull.vertices)
            {
                resource::endian::writeLE<float>(outFile, v.x);
                resource::endian::writeLE<float>(outFile, v.y);
                resource::endian::writeLE<float>(outFile, v.z);
            }

            resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(hull.indices.size()));
            for (uint32_t idx : hull.indices)
            {
                resource::endian::writeLE<uint32_t>(outFile, idx);
            }

            resource::endian::writeLE<float>(outFile, hull.center.x);
            resource::endian::writeLE<float>(outFile, hull.center.y);
            resource::endian::writeLE<float>(outFile, hull.center.z);
            resource::endian::writeLE<float>(outFile, hull.volume);
        }
    }

    void Mesh::writeSkeletonData(std::ofstream& outFile, const ExtractedSkeleton& skeleton) const
    {
        // Write hasSkinning flag
        resource::endian::writeLE<uint8_t>(outFile, skeleton.hasSkinning ? 1 : 0);

        if (!skeleton.hasSkinning)
        {
            return;
        }

        // Write bone count
        uint32_t boneCount = static_cast<uint32_t>(skeleton.boneNames.size());
        resource::endian::writeLE<uint32_t>(outFile, boneCount);

        // Write bone names
        for (const auto& name : skeleton.boneNames)
        {
            uint32_t nameLength = static_cast<uint32_t>(name.length());
            resource::endian::writeLE<uint32_t>(outFile, nameLength);
            if (nameLength > 0)
            {
                outFile.write(name.data(), nameLength);
            }
        }

        // Write inverse bind pose matrices (16 floats per matrix)
        for (const auto& matrix : skeleton.inverseBindPoses)
        {
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    resource::endian::writeLE<float>(outFile, matrix[col][row]);
                }
            }
        }

        vfLogInfo("Written skeleton data: {} bones", boneCount);
    }
}
