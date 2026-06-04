#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include "../resource/Types.hpp"

namespace destruction
{
    struct ClipResult
    {
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<std::pair<uint32_t, uint32_t>> cutEdges;
        bool didClip = false;
    };

    class MeshClipper
    {
    public:
        // Clip mesh to positive half-space of plane (dot(normal, p) + d >= 0)
        // plane.xyz = normal, plane.w = distance
        static ClipResult clipToHalfSpace(
            const std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            const glm::vec4& plane);

    private:
        static constexpr float EPSILON = 1e-6f;

        static float signedDistance(const glm::vec3& point, const glm::vec4& plane);

        static resource::Vertex interpolateVertex(
            const resource::Vertex& a,
            const resource::Vertex& b,
            float t);

        struct ClipContext
        {
            ClipResult& result;
            const std::vector<resource::Vertex>& vertices;
            const std::vector<float>& distances;
            std::unordered_map<uint32_t, uint32_t> vertexMap;
            std::unordered_map<uint64_t, uint32_t> edgeVertexCache;

            uint32_t getVertexIndex(uint32_t oldIndex);
            uint32_t getEdgeVertex(uint32_t idx0, uint32_t idx1);
            static uint64_t edgeKey(uint32_t a, uint32_t b);
        };

        static void processTriangles(
            ClipContext& ctx,
            const std::vector<uint32_t>& indices);

        static void clipTriangleOnePositive(
            ClipContext& ctx,
            uint32_t i0, uint32_t i1, uint32_t i2,
            bool p0, bool p1, bool p2);

        static void clipTriangleTwoPositive(
            ClipContext& ctx,
            uint32_t i0, uint32_t i1, uint32_t i2,
            bool p0, bool p1, bool p2);
    };
}
