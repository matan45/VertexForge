#pragma once
#include "Mesh.hpp"
#include "resource/ClusterDAGTypes.hpp"
#include <functional>
#include <vector>
#include <utility>

namespace types
{
    // Configuration for cluster DAG building
    struct ClusterDAGConfig
    {
        // Target triangles per leaf cluster (default 128 to match Nanite)
        uint32_t targetTrianglesPerCluster = resource::TARGET_CLUSTER_TRIANGLES;

        // Max meshlets that can be grouped into one cluster
        uint32_t maxMeshletsPerCluster = resource::MAX_MESHLETS_PER_CLUSTER;

        // Simplification target ratio for each DAG level (0.5 = 50% reduction)
        float simplificationRatio = 0.5f;

        // Maximum DAG depth (limits recursion)
        uint32_t maxDAGDepth = resource::MAX_DAG_DEPTH;
    };

    // Intermediate cluster representation during building
    struct BuildCluster
    {
        // Meshlet indices that belong to this cluster
        std::vector<uint32_t> meshletIndices;

        // Geometry for this cluster (used for simplification)
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;

        // Bounds
        glm::vec4 boundingSphere{0.0f};  // xyz = center, w = radius
        glm::vec4 cone{0.0f, 0.0f, 1.0f, 1.0f};  // xyz = axis, w = cos(half-angle)

        // Hierarchy info
        uint32_t parentIndex = resource::INVALID_CLUSTER_INDEX;
        uint32_t siblingIndex = resource::INVALID_CLUSTER_INDEX;
        uint32_t leftChildIndex = resource::INVALID_CLUSTER_INDEX;
        uint32_t rightChildIndex = resource::INVALID_CLUSTER_INDEX;

        // Error and level
        float geometricError = 0.0f;
        uint16_t level = 0;
        uint16_t flags = 0;
    };

    class ClusterDAGBuilder
    {
    public:
        using ProgressCallback = std::function<void(float)>;

        explicit ClusterDAGBuilder(const ClusterDAGConfig& config = {});

        // Main entry point: build cluster DAG from LOD0 mesh and its meshlets
        resource::ClusterDAGData build(
            const LODMeshData& lod0Mesh,
            const MeshletBuildResult& meshletResult,
            ProgressCallback progressCallback = nullptr) const;

    private:
        ClusterDAGConfig config_;

        // Phase 1: Create leaf clusters from meshlets
        std::vector<BuildCluster> createLeafClusters(
            const LODMeshData& mesh,
            const MeshletBuildResult& meshletResult) const;

        // Phase 2: Spatial grouping - pair clusters for hierarchy
        std::vector<std::pair<uint32_t, uint32_t>> groupClustersSpatially(
            const std::vector<BuildCluster>& clusters,
            uint32_t baseIndex) const;

        // Phase 3: Generate parent cluster with simplified geometry
        BuildCluster createParentCluster(
            const BuildCluster& leftChild,
            const BuildCluster& rightChild,
            uint32_t leftIndex,
            uint32_t rightIndex,
            uint16_t level) const;

        // Phase 4: Build hierarchy bottom-up
        std::vector<BuildCluster> buildHierarchy(
            std::vector<BuildCluster> leafClusters,
            ProgressCallback progressCallback) const;

        // Helper: Extract geometry from meshlet data
        void extractClusterGeometry(
            BuildCluster& cluster,
            const LODMeshData& mesh,
            const MeshletBuildResult& meshletResult) const;

        // Helper: Compute bounding sphere from vertex positions
        glm::vec4 computeBoundingSphere(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices) const;

        // Helper: Compute normal cone for backface culling
        glm::vec4 computeNormalCone(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices) const;

        // Helper: Merge bounding spheres
        glm::vec4 mergeBoundingSpheres(const glm::vec4& a, const glm::vec4& b) const;

        // Result of mesh simplification
        struct SimplificationResult
        {
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> indices;
            float geometricError = 0.0f;
        };

        // Helper: Simplify mesh and return geometric error
        SimplificationResult simplifyClusterGeometry(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            float targetRatio) const;

        // Helper: Convert BuildClusters to final ClusterDAGData
        resource::ClusterDAGData finalizeClusters(
            std::vector<BuildCluster>& clusters) const;
    };
}
