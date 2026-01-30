#include "ClusterDAGBuilder.hpp"
#include "print/EditorLogger.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>
#include <numeric>

namespace
{
    // Morton code helpers for spatial sorting (Z-order curve)
    // Expands a 10-bit integer into 30 bits by inserting 2 zeros between each bit
    uint32_t expandBits(uint32_t v)
    {
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }

    // Calculates a 30-bit Morton code for a 3D point in [0,1] range
    uint32_t morton3D(float x, float y, float z)
    {
        x = glm::clamp(x, 0.0f, 1.0f) * 1023.0f;
        y = glm::clamp(y, 0.0f, 1.0f) * 1023.0f;
        z = glm::clamp(z, 0.0f, 1.0f) * 1023.0f;
        uint32_t xx = expandBits(static_cast<uint32_t>(x));
        uint32_t yy = expandBits(static_cast<uint32_t>(y));
        uint32_t zz = expandBits(static_cast<uint32_t>(z));
        return (xx << 2) | (yy << 1) | zz;
    }
}

namespace
{
    // Helper: compute Morton code for a cluster center
    uint32_t computeClusterMorton(const glm::vec4& boundingSphere,
                                   const glm::vec3& minBounds,
                                   const glm::vec3& invExtent)
    {
        glm::vec3 center(boundingSphere);
        glm::vec3 normalized = (center - minBounds) * invExtent;
        return morton3D(normalized.x, normalized.y, normalized.z);
    }
}

namespace types
{
    ClusterDAGBuilder::ClusterDAGBuilder(const ClusterDAGConfig& config)
        : config_(config)
    {
    }

    resource::ClusterDAGData ClusterDAGBuilder::build(
        const LODMeshData& lod0Mesh,
        const MeshletBuildResult& meshletResult,
        ProgressCallback progressCallback) const
    {
        resource::ClusterDAGData result;

        if (meshletResult.meshlets.empty())
        {
            vfLogWarning("ClusterDAGBuilder: No meshlets to build DAG from");
            return result;
        }

        // Phase 1: Create leaf clusters from meshlets
        auto leafClusters = createLeafClusters(lod0Mesh, meshletResult);

        if (leafClusters.empty())
        {
            vfLogWarning("ClusterDAGBuilder: Failed to create leaf clusters");
            return result;
        }

        vfLogInfo("    Created {} leaf clusters from {} meshlets",
                  leafClusters.size(), meshletResult.meshlets.size());

        // Phase 2-4: Build hierarchy bottom-up
        auto allClusters = buildHierarchy(std::move(leafClusters), progressCallback);

        if (allClusters.empty())
        {
            vfLogWarning("ClusterDAGBuilder: Failed to build hierarchy");
            return result;
        }

        // Phase 5: Finalize and convert to output format
        result = finalizeClusters(allClusters);

        if (progressCallback)
        {
            progressCallback(1.0f);
        }

        return result;
    }

    std::vector<BuildCluster> ClusterDAGBuilder::createLeafClusters(
        const LODMeshData& mesh,
        const MeshletBuildResult& meshletResult) const
    {
        std::vector<BuildCluster> leafClusters;

        if (meshletResult.meshlets.empty())
        {
            return leafClusters;
        }

        uint32_t meshletIdx = 0;
        while (meshletIdx < static_cast<uint32_t>(meshletResult.meshlets.size()))
        {
            BuildCluster cluster;
            uint32_t triangleCount = 0;

            // Greedily add meshlets until we hit target or max count
            while (meshletIdx < static_cast<uint32_t>(meshletResult.meshlets.size()) &&
                   cluster.meshletIndices.size() < config_.maxMeshletsPerCluster &&
                   triangleCount < config_.targetTrianglesPerCluster)
            {
                const auto& meshlet = meshletResult.meshlets[meshletIdx];
                cluster.meshletIndices.push_back(meshletIdx);
                triangleCount += meshlet.descriptor.primitiveCount;
                ++meshletIdx;
            }

            // Extract geometry for this cluster
            extractClusterGeometry(cluster, mesh, meshletResult);

            // Compute bounds
            if (!cluster.indices.empty())
            {
                cluster.boundingSphere = computeBoundingSphere(cluster.vertices, cluster.indices);
                cluster.cone = computeNormalCone(cluster.vertices, cluster.indices);
            }

            // Mark as leaf
            cluster.flags = resource::ClusterFlags::IsLeaf;
            cluster.geometricError = 0.0f;
            cluster.level = 0;  // Will be updated in buildHierarchy

            leafClusters.push_back(std::move(cluster));
        }

        return leafClusters;
    }

    void ClusterDAGBuilder::extractClusterGeometry(
        BuildCluster& cluster,
        const LODMeshData& mesh,
        const MeshletBuildResult& meshletResult) const
    {
        // Collect all unique vertex indices used by this cluster's meshlets
        std::unordered_set<uint32_t> usedVertexIndices;

        for (uint32_t meshletIdx : cluster.meshletIndices)
        {
            const auto& meshlet = meshletResult.meshlets[meshletIdx];

            // Get vertex indices for this meshlet
            for (uint8_t v = 0; v < meshlet.descriptor.vertexCount; ++v)
            {
                uint32_t globalVertexIdx = meshletResult.meshletVertices[
                    meshlet.descriptor.vertexOffset + v];
                usedVertexIndices.insert(globalVertexIdx);
            }
        }

        // Create local vertex buffer and remap
        std::unordered_map<uint32_t, uint32_t> globalToLocal;
        cluster.vertices.reserve(usedVertexIndices.size());

        for (uint32_t globalIdx : usedVertexIndices)
        {
            if (globalIdx < mesh.vertices.size())
            {
                globalToLocal[globalIdx] = static_cast<uint32_t>(cluster.vertices.size());
                cluster.vertices.push_back(mesh.vertices[globalIdx]);
            }
        }

        // Extract triangles for this cluster
        for (uint32_t meshletIdx : cluster.meshletIndices)
        {
            const auto& meshlet = meshletResult.meshlets[meshletIdx];

            for (uint8_t t = 0; t < meshlet.descriptor.primitiveCount; ++t)
            {
                uint32_t packed = meshletResult.meshletPrimitives[
                    meshlet.descriptor.primitiveOffset + t];

                uint8_t localIdx0 = static_cast<uint8_t>(packed & 0xFF);
                uint8_t localIdx1 = static_cast<uint8_t>((packed >> 8) & 0xFF);
                uint8_t localIdx2 = static_cast<uint8_t>((packed >> 16) & 0xFF);

                // Convert meshlet-local indices to global, then to cluster-local
                uint32_t globalIdx0 = meshletResult.meshletVertices[
                    meshlet.descriptor.vertexOffset + localIdx0];
                uint32_t globalIdx1 = meshletResult.meshletVertices[
                    meshlet.descriptor.vertexOffset + localIdx1];
                uint32_t globalIdx2 = meshletResult.meshletVertices[
                    meshlet.descriptor.vertexOffset + localIdx2];

                auto it0 = globalToLocal.find(globalIdx0);
                auto it1 = globalToLocal.find(globalIdx1);
                auto it2 = globalToLocal.find(globalIdx2);

                if (it0 != globalToLocal.end() && it1 != globalToLocal.end() && it2 != globalToLocal.end())
                {
                    cluster.indices.push_back(it0->second);
                    cluster.indices.push_back(it1->second);
                    cluster.indices.push_back(it2->second);
                }
            }
        }
    }

    std::vector<std::pair<uint32_t, uint32_t>> ClusterDAGBuilder::groupClustersSpatially(
        const BuildCluster* clusters,
        size_t clusterCount,
        uint32_t baseIndex) const
    {
        std::vector<std::pair<uint32_t, uint32_t>> pairs;

        if (clusterCount == 0)
        {
            return pairs;
        }

        if (clusterCount == 1)
        {
            pairs.emplace_back(baseIndex, resource::INVALID_CLUSTER_INDEX);
            return pairs;
        }

        // Compute bounding box of all cluster centers for normalization
        glm::vec3 minBounds(FLT_MAX);
        glm::vec3 maxBounds(-FLT_MAX);

        for (size_t i = 0; i < clusterCount; ++i)
        {
            glm::vec3 center(clusters[i].boundingSphere);
            minBounds = glm::min(minBounds, center);
            maxBounds = glm::max(maxBounds, center);
        }

        glm::vec3 extent = maxBounds - minBounds;
        glm::vec3 invExtent(
            extent.x > 1e-6f ? 1.0f / extent.x : 0.0f,
            extent.y > 1e-6f ? 1.0f / extent.y : 0.0f,
            extent.z > 1e-6f ? 1.0f / extent.z : 0.0f
        );

        // Compute Morton codes and create sorted indices
        std::vector<std::pair<uint32_t, uint32_t>> mortonIndices;  // (morton code, original index)
        mortonIndices.reserve(clusterCount);

        for (uint32_t i = 0; i < static_cast<uint32_t>(clusterCount); ++i)
        {
            glm::vec3 center(clusters[i].boundingSphere);
            glm::vec3 normalized = (center - minBounds) * invExtent;
            uint32_t morton = morton3D(normalized.x, normalized.y, normalized.z);
            mortonIndices.emplace_back(morton, i);
        }

        // Sort by Morton code - O(n log n)
        std::sort(mortonIndices.begin(), mortonIndices.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        // Pair adjacent clusters in Morton order
        // This gives good spatial locality due to Z-order curve properties
        pairs.reserve((clusterCount + 1) / 2);

        for (size_t i = 0; i + 1 < mortonIndices.size(); i += 2)
        {
            uint32_t idx1 = mortonIndices[i].second;
            uint32_t idx2 = mortonIndices[i + 1].second;
            pairs.emplace_back(baseIndex + idx1, baseIndex + idx2);
        }

        // Handle odd cluster
        if (mortonIndices.size() % 2 == 1)
        {
            uint32_t lastIdx = mortonIndices.back().second;
            pairs.emplace_back(baseIndex + lastIdx, resource::INVALID_CLUSTER_INDEX);
        }

        return pairs;
    }

    BuildCluster ClusterDAGBuilder::createParentCluster(
        const BuildCluster& leftChild,
        const BuildCluster& rightChild,
        uint32_t leftIndex,
        uint32_t rightIndex,
        uint16_t level) const
    {
        BuildCluster parent;

        // Merge child geometry
        parent.vertices = leftChild.vertices;
        parent.indices = leftChild.indices;

        // Append right child with offset indices
        uint32_t indexOffset = static_cast<uint32_t>(parent.vertices.size());
        parent.vertices.insert(parent.vertices.end(),
                               rightChild.vertices.begin(),
                               rightChild.vertices.end());

        for (uint32_t idx : rightChild.indices)
        {
            parent.indices.push_back(idx + indexOffset);
        }

        // Merge meshlet indices from children
        parent.meshletIndices = leftChild.meshletIndices;
        parent.meshletIndices.insert(parent.meshletIndices.end(),
                                     rightChild.meshletIndices.begin(),
                                     rightChild.meshletIndices.end());

        // Simplify merged geometry
        auto simplified = simplifyClusterGeometry(
            parent.vertices,
            parent.indices,
            config_.simplificationRatio);

        parent.vertices = std::move(simplified.vertices);
        parent.indices = std::move(simplified.indices);

        // Propagate max error from children and simplification
        parent.geometricError = std::max({
            simplified.geometricError,
            leftChild.geometricError,
            rightChild.geometricError
        });

        // Merge bounds
        parent.boundingSphere = mergeBoundingSpheres(
            leftChild.boundingSphere,
            rightChild.boundingSphere);

        // Recompute normal cone for simplified geometry
        if (!parent.indices.empty())
        {
            parent.cone = computeNormalCone(parent.vertices, parent.indices);
        }

        // Set hierarchy links
        parent.leftChildIndex = leftIndex;
        parent.rightChildIndex = rightIndex;
        parent.level = level;
        parent.flags = resource::ClusterFlags::HasLeftChild | resource::ClusterFlags::HasRightChild;

        return parent;
    }

    std::vector<BuildCluster> ClusterDAGBuilder::buildHierarchy(
        std::vector<BuildCluster> leafClusters,
        ProgressCallback progressCallback) const
    {
        std::vector<BuildCluster> allClusters;

        if (leafClusters.empty())
        {
            return allClusters;
        }

        // Special case: single leaf cluster becomes root
        if (leafClusters.size() == 1)
        {
            leafClusters[0].flags |= resource::ClusterFlags::IsRoot;
            leafClusters[0].level = 0;
            allClusters.push_back(std::move(leafClusters[0]));
            return allClusters;
        }

        // Start with leaf clusters as the current level
        std::vector<BuildCluster> currentLevel = std::move(leafClusters);
        uint16_t maxLevel = 0;

        // Estimate total clusters for progress
        uint32_t totalClustersEstimate = static_cast<uint32_t>(currentLevel.size()) * 2;
        uint32_t processedClusters = 0;

        while (currentLevel.size() > 1 && maxLevel < config_.maxDAGDepth)
        {
            // Record base index for this level
            uint32_t levelBaseIndex = static_cast<uint32_t>(allClusters.size());

            // Save count before moving clusters
            size_t clustersInThisLevel = currentLevel.size();

            // Set level for current clusters and add to result
            for (auto& cluster : currentLevel)
            {
                cluster.level = maxLevel;
                allClusters.push_back(std::move(cluster));
            }

            processedClusters += static_cast<uint32_t>(clustersInThisLevel);
            currentLevel.clear();

            // Group clusters spatially - pass pointer to this level's clusters (no copy)
            auto pairs = groupClustersSpatially(
                allClusters.data() + levelBaseIndex,
                clustersInThisLevel,
                levelBaseIndex);

            // Create parent level
            std::vector<BuildCluster> nextLevel;
            nextLevel.reserve(pairs.size());

            for (const auto& [leftIdx, rightIdx] : pairs)
            {
                if (rightIdx != resource::INVALID_CLUSTER_INDEX)
                {
                    auto parent = createParentCluster(
                        allClusters[leftIdx],
                        allClusters[rightIdx],
                        leftIdx,
                        rightIdx,
                        maxLevel + 1);

                    // Update children with parent reference
                    uint32_t parentIdx = static_cast<uint32_t>(allClusters.size() + nextLevel.size());
                    allClusters[leftIdx].parentIndex = parentIdx;
                    allClusters[rightIdx].parentIndex = parentIdx;
                    allClusters[leftIdx].siblingIndex = rightIdx;
                    allClusters[rightIdx].siblingIndex = leftIdx;

                    nextLevel.push_back(std::move(parent));
                }
                else
                {
                    // Single cluster promotes to next level
                    BuildCluster promoted = allClusters[leftIdx];
                    promoted.level = maxLevel + 1;
                    // Clear child indices since this is a promoted cluster
                    promoted.leftChildIndex = resource::INVALID_CLUSTER_INDEX;
                    promoted.rightChildIndex = resource::INVALID_CLUSTER_INDEX;
                    nextLevel.push_back(std::move(promoted));
                }
            }

            currentLevel = std::move(nextLevel);
            ++maxLevel;

            if (progressCallback)
            {
                float progress = static_cast<float>(processedClusters) /
                                 static_cast<float>(totalClustersEstimate);
                progressCallback(std::min(progress, 0.95f));
            }
        }

        // Add final root cluster(s)
        for (auto& cluster : currentLevel)
        {
            cluster.flags |= resource::ClusterFlags::IsRoot;
            cluster.level = maxLevel;
            allClusters.push_back(std::move(cluster));
        }

        return allClusters;
    }

    glm::vec4 ClusterDAGBuilder::computeBoundingSphere(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices) const
    {
        if (indices.empty() || vertices.empty())
        {
            return glm::vec4(0.0f);
        }

        // Compute center as average of used vertices
        glm::vec3 center(0.0f);
        std::unordered_set<uint32_t> usedIndices(indices.begin(), indices.end());

        for (uint32_t idx : usedIndices)
        {
            if (idx < vertices.size())
            {
                center += vertices[idx].position;
            }
        }
        center /= static_cast<float>(usedIndices.size());

        // Compute radius as max distance from center
        float radius = 0.0f;
        for (uint32_t idx : usedIndices)
        {
            if (idx < vertices.size())
            {
                float dist = glm::distance(center, vertices[idx].position);
                radius = std::max(radius, dist);
            }
        }

        return glm::vec4(center, radius);
    }

    glm::vec4 ClusterDAGBuilder::computeNormalCone(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices) const
    {
        if (indices.size() < 3 || vertices.empty())
        {
            return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);  // Disable backface culling
        }

        // Compute average normal direction
        glm::vec3 avgNormal(0.0f);
        uint32_t triangleCount = 0;

        for (size_t i = 0; i + 3 <= indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                continue;
            }

            const glm::vec3& v0 = vertices[i0].position;
            const glm::vec3& v1 = vertices[i1].position;
            const glm::vec3& v2 = vertices[i2].position;

            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 normal = glm::cross(edge1, edge2);

            float len = glm::length(normal);
            if (len > 1e-6f)
            {
                avgNormal += normal / len;
                ++triangleCount;
            }
        }

        if (triangleCount == 0)
        {
            return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);  // Disable backface culling
        }

        avgNormal = glm::normalize(avgNormal);

        // Compute cone angle (max deviation from average normal)
        float minCosAngle = 1.0f;

        for (size_t i = 0; i + 3 <= indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                continue;
            }

            const glm::vec3& v0 = vertices[i0].position;
            const glm::vec3& v1 = vertices[i1].position;
            const glm::vec3& v2 = vertices[i2].position;

            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 normal = glm::cross(edge1, edge2);

            float len = glm::length(normal);
            if (len > 1e-6f)
            {
                normal /= len;
                float cosAngle = glm::dot(avgNormal, normal);
                minCosAngle = std::min(minCosAngle, cosAngle);
            }
        }

        // If normals vary too much, disable backface culling
        if (minCosAngle < -0.5f)
        {
            return glm::vec4(avgNormal, 1.0f);  // cos >= 1.0 disables culling
        }

        return glm::vec4(avgNormal, minCosAngle);
    }

    glm::vec4 ClusterDAGBuilder::mergeBoundingSpheres(const glm::vec4& a, const glm::vec4& b) const
    {
        glm::vec3 centerA(a);
        glm::vec3 centerB(b);
        float radiusA = a.w;
        float radiusB = b.w;

        glm::vec3 d = centerB - centerA;
        float dist = glm::length(d);

        // If one sphere contains the other
        if (dist + radiusA <= radiusB)
        {
            return b;
        }
        if (dist + radiusB <= radiusA)
        {
            return a;
        }

        // General case: compute bounding sphere of both
        float newRadius = (dist + radiusA + radiusB) * 0.5f;
        glm::vec3 newCenter;

        if (dist > 1e-6f)
        {
            newCenter = centerA + d * ((newRadius - radiusA) / dist);
        }
        else
        {
            newCenter = centerA;
        }

        return glm::vec4(newCenter, newRadius);
    }

    ClusterDAGBuilder::SimplificationResult ClusterDAGBuilder::simplifyClusterGeometry(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        float targetRatio) const
    {
        SimplificationResult result;

        if (indices.empty() || vertices.empty())
        {
            return result;
        }

        if (indices.size() < 9)  // Less than 3 triangles, don't simplify
        {
            result.vertices = vertices;
            result.indices = indices;
            result.geometricError = 0.0f;
            return result;
        }

        size_t targetIndexCount = static_cast<size_t>(
            static_cast<float>(indices.size()) * targetRatio);
        targetIndexCount = std::max(targetIndexCount, static_cast<size_t>(3));
        targetIndexCount = (targetIndexCount / 3) * 3;

        // Get scale factor for error conversion
        float scale = meshopt_simplifyScale(
            reinterpret_cast<const float*>(vertices.data()),
            vertices.size(),
            sizeof(resource::Vertex));

        // Simplify with error tracking
        std::vector<uint32_t> simplifiedIndices(indices.size());
        float resultError = 0.0f;

        size_t actualIndexCount = meshopt_simplify(
            simplifiedIndices.data(),
            indices.data(),
            indices.size(),
            reinterpret_cast<const float*>(vertices.data()),
            vertices.size(),
            sizeof(resource::Vertex),
            targetIndexCount,
            FLT_MAX,  // Allow any error to reach target
            0,        // options
            &resultError);

        simplifiedIndices.resize(actualIndexCount);

        // Convert relative error to absolute
        result.geometricError = resultError * scale;

        // Remap vertices to only include used ones
        std::vector<unsigned int> remap(vertices.size(), ~0u);
        size_t uniqueVertexCount = 0;

        for (uint32_t idx : simplifiedIndices)
        {
            if (remap[idx] == ~0u)
            {
                remap[idx] = static_cast<unsigned int>(uniqueVertexCount++);
            }
        }

        result.vertices.resize(uniqueVertexCount);
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            if (remap[i] != ~0u)
            {
                result.vertices[remap[i]] = vertices[i];
            }
        }

        result.indices.reserve(simplifiedIndices.size());
        for (uint32_t idx : simplifiedIndices)
        {
            result.indices.push_back(remap[idx]);
        }

        return result;
    }

    resource::ClusterDAGData ClusterDAGBuilder::finalizeClusters(
        std::vector<BuildCluster>& clusters) const
    {
        resource::ClusterDAGData result;

        if (clusters.empty())
        {
            return result;
        }

        // Find root cluster (the one with IsRoot flag)
        uint32_t rootIndex = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(clusters.size()); ++i)
        {
            if (clusters[i].flags & resource::ClusterFlags::IsRoot)
            {
                rootIndex = i;
                break;
            }
        }

        // If root is not at index 0, we need to swap and fix indices
        if (rootIndex != 0)
        {
            std::swap(clusters[0], clusters[rootIndex]);

            // Fix all references to swapped indices
            for (auto& cluster : clusters)
            {
                if (cluster.parentIndex == 0)
                    cluster.parentIndex = rootIndex;
                else if (cluster.parentIndex == rootIndex)
                    cluster.parentIndex = 0;

                if (cluster.siblingIndex == 0)
                    cluster.siblingIndex = rootIndex;
                else if (cluster.siblingIndex == rootIndex)
                    cluster.siblingIndex = 0;

                if (cluster.leftChildIndex == 0)
                    cluster.leftChildIndex = rootIndex;
                else if (cluster.leftChildIndex == rootIndex)
                    cluster.leftChildIndex = 0;

                if (cluster.rightChildIndex == 0)
                    cluster.rightChildIndex = rootIndex;
                else if (cluster.rightChildIndex == rootIndex)
                    cluster.rightChildIndex = 0;
            }
        }

        // Count leaves and find max depth
        uint32_t leafCount = 0;
        uint32_t maxDepth = 0;

        for (const auto& cluster : clusters)
        {
            if (cluster.flags & resource::ClusterFlags::IsLeaf)
            {
                ++leafCount;
            }
            maxDepth = std::max(maxDepth, static_cast<uint32_t>(cluster.level));
        }

        // Fill header
        result.header.clusterCount = static_cast<uint32_t>(clusters.size());
        result.header.leafClusterCount = leafCount;
        result.header.maxDepth = maxDepth;
        result.header.maxGeometricError = clusters[0].geometricError;  // Root error
        result.header.boundingSphere = clusters[0].boundingSphere;

        // Convert BuildCluster to resource::Cluster
        result.clusters.reserve(clusters.size());

        for (const auto& build : clusters)
        {
            resource::Cluster cluster;

            // Descriptor - stores meshlet info
            cluster.descriptor.meshletOffset = build.meshletIndices.empty() ? 0 :
                                               build.meshletIndices[0];
            cluster.descriptor.meshletCount = static_cast<uint16_t>(build.meshletIndices.size());
            cluster.descriptor.triangleCount = static_cast<uint16_t>(build.indices.size() / 3);
            cluster.descriptor.vertexOffset = 0;  // Will be set during rendering
            cluster.descriptor.vertexCount = static_cast<uint32_t>(build.vertices.size());

            // Bounds
            cluster.bounds.boundingSphere = build.boundingSphere;
            cluster.bounds.cone = build.cone;

            // Hierarchy
            cluster.hierarchy.parentIndex = build.parentIndex;
            cluster.hierarchy.siblingIndex = build.siblingIndex;
            cluster.hierarchy.geometricError = build.geometricError;
            cluster.hierarchy.level = build.level;
            cluster.hierarchy.flags = build.flags;

            result.clusters.push_back(cluster);
        }

        // VK-295: Generate streaming units for efficient I/O
        result.streamingUnits = generateStreamingUnits(result.clusters);
        result.header.streamingUnitCount = static_cast<uint32_t>(result.streamingUnits.size());

        // Find root streaming unit (the one containing cluster 0)
        result.header.rootStreamingUnit = result.getStreamingUnitIndex(0);

        vfLogInfo("    Generated {} streaming units", result.streamingUnits.size());

        return result;
    }

    std::vector<resource::ClusterStreamingUnit> ClusterDAGBuilder::generateStreamingUnits(
        const std::vector<resource::Cluster>& clusters) const
    {
        std::vector<resource::ClusterStreamingUnit> units;

        if (clusters.empty())
        {
            return units;
        }

        // Step 1: Group cluster indices by level (root level 0, leaves have highest level)
        // Since root is at index 0 and has the lowest level, process levels 0 to maxLevel
        uint32_t maxLevel = 0;
        for (const auto& cluster : clusters)
        {
            maxLevel = std::max(maxLevel, static_cast<uint32_t>(cluster.hierarchy.level));
        }

        std::vector<std::vector<uint32_t>> levelToClusters(maxLevel + 1);
        for (uint32_t i = 0; i < static_cast<uint32_t>(clusters.size()); ++i)
        {
            uint16_t level = clusters[i].hierarchy.level;
            levelToClusters[level].push_back(i);
        }

        // Step 2: Compute bounding box for Morton code normalization
        glm::vec3 minBounds(FLT_MAX);
        glm::vec3 maxBounds(-FLT_MAX);

        for (const auto& cluster : clusters)
        {
            glm::vec3 center(cluster.bounds.boundingSphere);
            minBounds = glm::min(minBounds, center);
            maxBounds = glm::max(maxBounds, center);
        }

        glm::vec3 extent = maxBounds - minBounds;
        glm::vec3 invExtent(
            extent.x > 1e-6f ? 1.0f / extent.x : 0.0f,
            extent.y > 1e-6f ? 1.0f / extent.y : 0.0f,
            extent.z > 1e-6f ? 1.0f / extent.z : 0.0f
        );

        // Step 3: Track which cluster belongs to which streaming unit
        std::vector<uint32_t> clusterToUnit(clusters.size(), resource::INVALID_STREAMING_UNIT_INDEX);

        // Step 4: Process levels from root (0) to leaves (maxLevel) - parent-before-children
        for (uint32_t level = 0; level <= maxLevel; ++level)
        {
            const auto& clusterIndices = levelToClusters[level];
            if (clusterIndices.empty())
            {
                continue;
            }

            // Sort cluster indices at this level by Morton code for spatial locality
            std::vector<std::pair<uint32_t, uint32_t>> mortonIndices;  // (morton, clusterIndex)
            mortonIndices.reserve(clusterIndices.size());

            for (uint32_t clusterIdx : clusterIndices)
            {
                uint32_t morton = computeClusterMorton(
                    clusters[clusterIdx].bounds.boundingSphere,
                    minBounds,
                    invExtent);
                mortonIndices.emplace_back(morton, clusterIdx);
            }

            std::sort(mortonIndices.begin(), mortonIndices.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            // Greedily group adjacent clusters into streaming units
            size_t idx = 0;
            while (idx < mortonIndices.size())
            {
                resource::ClusterStreamingUnit unit{};
                unit.clusterStartIndex = mortonIndices[idx].second;
                unit.clusterCount = 0;
                unit.meshletStartOffset = UINT32_MAX;
                unit.meshletCount = 0;
                unit.minGeometricError = FLT_MAX;
                unit.maxGeometricError = 0.0f;
                unit.minLevel = UINT16_MAX;
                unit.maxLevel = 0;
                unit.dependsOnUnit = resource::INVALID_STREAMING_UNIT_INDEX;
                unit.boundingSphere = glm::vec4(0.0f);

                // Find min cluster index among candidates (for contiguous range)
                uint32_t minClusterIdx = UINT32_MAX;
                uint32_t maxClusterIdx = 0;

                // Collect clusters for this unit (4-16 clusters, but respect level)
                std::vector<uint32_t> unitClusters;
                while (idx < mortonIndices.size() &&
                       unitClusters.size() < resource::MAX_CLUSTERS_PER_STREAMING_UNIT)
                {
                    uint32_t clusterIdx = mortonIndices[idx].second;
                    unitClusters.push_back(clusterIdx);
                    minClusterIdx = std::min(minClusterIdx, clusterIdx);
                    maxClusterIdx = std::max(maxClusterIdx, clusterIdx);
                    ++idx;

                    // Stop at minimum size if we've hit target
                    if (unitClusters.size() >= resource::MIN_CLUSTERS_PER_STREAMING_UNIT &&
                        idx < mortonIndices.size())
                    {
                        // Check if next cluster is far away (Morton distance heuristic)
                        uint32_t currentMorton = mortonIndices[idx - 1].first;
                        uint32_t nextMorton = mortonIndices[idx].first;
                        uint32_t mortonDist = (nextMorton > currentMorton)
                            ? (nextMorton - currentMorton)
                            : (currentMorton - nextMorton);

                        // If large spatial gap, break here
                        if (mortonDist > 0x10000)  // ~1/16 of Morton range
                        {
                            break;
                        }
                    }
                }

                // Fill streaming unit data
                unit.clusterStartIndex = minClusterIdx;
                unit.clusterCount = static_cast<uint32_t>(unitClusters.size());

                for (uint32_t clusterIdx : unitClusters)
                {
                    const auto& cluster = clusters[clusterIdx];

                    // Aggregate meshlet info
                    if (cluster.descriptor.meshletOffset < unit.meshletStartOffset)
                    {
                        unit.meshletStartOffset = cluster.descriptor.meshletOffset;
                    }
                    unit.meshletCount += cluster.descriptor.meshletCount;

                    // Aggregate error bounds
                    unit.minGeometricError = std::min(unit.minGeometricError,
                                                       cluster.hierarchy.geometricError);
                    unit.maxGeometricError = std::max(unit.maxGeometricError,
                                                       cluster.hierarchy.geometricError);

                    // Aggregate level range
                    unit.minLevel = std::min(unit.minLevel, cluster.hierarchy.level);
                    unit.maxLevel = std::max(unit.maxLevel, cluster.hierarchy.level);

                    // Merge bounding spheres
                    if (unit.boundingSphere.w == 0.0f)
                    {
                        unit.boundingSphere = cluster.bounds.boundingSphere;
                    }
                    else
                    {
                        unit.boundingSphere = mergeBoundingSpheres(
                            unit.boundingSphere,
                            cluster.bounds.boundingSphere);
                    }

                    // Record this cluster's unit assignment
                    clusterToUnit[clusterIdx] = static_cast<uint32_t>(units.size());
                }

                // Determine dependency: find parent cluster's unit
                // Use the first cluster in the unit that has a valid parent
                for (uint32_t clusterIdx : unitClusters)
                {
                    const auto& cluster = clusters[clusterIdx];
                    if (cluster.hierarchy.parentIndex != resource::INVALID_CLUSTER_INDEX)
                    {
                        uint32_t parentUnit = clusterToUnit[cluster.hierarchy.parentIndex];
                        if (parentUnit != resource::INVALID_STREAMING_UNIT_INDEX &&
                            parentUnit != static_cast<uint32_t>(units.size()))
                        {
                            unit.dependsOnUnit = parentUnit;
                            break;
                        }
                    }
                }

                units.push_back(unit);
            }
        }

        return units;
    }

    glm::vec4 ClusterDAGBuilder::mergeStreamingUnitBounds(
        const std::vector<resource::Cluster>& clusters,
        uint32_t startIndex,
        uint32_t count) const
    {
        glm::vec4 merged(0.0f);

        for (uint32_t i = 0; i < count && (startIndex + i) < clusters.size(); ++i)
        {
            const auto& cluster = clusters[startIndex + i];
            if (merged.w == 0.0f)
            {
                merged = cluster.bounds.boundingSphere;
            }
            else
            {
                merged = mergeBoundingSpheres(merged, cluster.bounds.boundingSphere);
            }
        }

        return merged;
    }
}
