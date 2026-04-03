#pragma once
#include "DestructionTypes.hpp"
#include <functional>
#include <atomic>
#include <random>
#include <string_view>

namespace destruction
{
    using FractureProgressCallback = std::function<void(float progress, std::string_view stage)>;

    class VoronoiFracture
    {
    public:
        static FractureResult fracture(
            const resource::MeshData& inputMesh,
            const FractureConfig& config,
            FractureProgressCallback progressCallback = nullptr,
            std::atomic<bool>* cancelFlag = nullptr);

        static resource::MeshesData toMeshesData(const FractureResult& result);

    private:
        static std::vector<glm::vec3> generateSeeds(
            const glm::vec3& bboxMin,
            const glm::vec3& bboxMax,
            const FractureConfig& config);

        static FragmentData buildFragment(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            const std::vector<glm::vec3>& seeds,
            uint32_t cellIndex,
            const FractureConfig& config);

        static void buildConnectivityGraph(
            std::vector<FragmentData>& fragments,
            const std::vector<glm::vec3>& seeds);

        static glm::vec3 computeCenterOfMass(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices);

        static float computeVolume(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices);

        static void capCutFace(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            const std::vector<std::pair<uint32_t, uint32_t>>& cutEdges,
            const glm::vec4& plane,
            float uvScale);

        static void computeBoundingBox(
            const std::vector<resource::Vertex>& vertices,
            glm::vec3& outMin,
            glm::vec3& outMax);

        static inline float signedTetraVolume(
            const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

        static std::vector<glm::vec3> generateUniformSeeds(
            const glm::vec3& bboxMin,
            const glm::vec3& bboxMax,
            const FractureConfig& config,
            std::mt19937& rng);

        static std::vector<glm::vec3> generateClusteredSeeds(
            const glm::vec3& bboxMin,
            const glm::vec3& bboxMax,
            const FractureConfig& config,
            std::mt19937& rng);

        struct PlaneClipInfo
        {
            glm::vec4 plane;
            std::vector<std::pair<uint32_t, uint32_t>> cutEdges;
            uint32_t neighborSeedIndex;
        };

        static void clipMeshToVoronoiCell(
            std::vector<resource::Vertex>& currentVertices,
            std::vector<uint32_t>& currentIndices,
            std::vector<PlaneClipInfo>& clipInfos,
            const std::vector<glm::vec3>& seeds,
            uint32_t cellIndex,
            float innerUVScale);

        static std::vector<std::vector<uint32_t>> buildEdgeLoops(
            const std::vector<std::pair<uint32_t, uint32_t>>& cutEdges);

        static bool isVoronoiNeighbor(
            const std::vector<glm::vec3>& seeds,
            uint32_t cellI,
            uint32_t cellJ);

        static bool generateFragments(
            std::vector<FragmentData>& outFragments,
            const resource::LODLevel& lod0,
            const std::vector<glm::vec3>& seeds,
            const FractureConfig& config,
            FractureProgressCallback& progressCallback,
            std::atomic<bool>* cancelFlag);
    };
}
