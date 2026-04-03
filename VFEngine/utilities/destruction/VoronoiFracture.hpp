#pragma once
#include "DestructionTypes.hpp"
#include <functional>
#include <atomic>
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
    };
}
