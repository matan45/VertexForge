#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <utility>
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
    };
}
