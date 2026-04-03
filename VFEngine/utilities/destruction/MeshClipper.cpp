#include "MeshClipper.hpp"
#include <algorithm>

namespace destruction
{
    float MeshClipper::signedDistance(const glm::vec3& point, const glm::vec4& plane)
    {
        return glm::dot(glm::vec3(plane), point) + plane.w;
    }

    resource::Vertex MeshClipper::interpolateVertex(
        const resource::Vertex& a,
        const resource::Vertex& b,
        float t)
    {
        resource::Vertex result;
        result.position = glm::mix(a.position, b.position, t);
        glm::vec3 mixedNormal = glm::mix(a.normal, b.normal, t);
        float len = glm::length(mixedNormal);
        result.normal = (len > 1e-7f) ? mixedNormal / len : a.normal;
        result.texCoords = glm::mix(a.texCoords, b.texCoords, t);
        result.boneIndices = a.boneIndices;
        result.boneWeights = glm::mix(a.boneWeights, b.boneWeights, t);
        return result;
    }

    uint64_t MeshClipper::ClipContext::edgeKey(uint32_t a, uint32_t b)
    {
        if (a > b) std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
    }

    uint32_t MeshClipper::ClipContext::getVertexIndex(uint32_t oldIndex)
    {
        auto it = vertexMap.find(oldIndex);
        if (it != vertexMap.end()) return it->second;
        uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
        result.vertices.push_back(vertices[oldIndex]);
        vertexMap[oldIndex] = newIndex;
        return newIndex;
    }

    uint32_t MeshClipper::ClipContext::getEdgeVertex(uint32_t idx0, uint32_t idx1)
    {
        uint64_t key = edgeKey(idx0, idx1);
        auto it = edgeVertexCache.find(key);
        if (it != edgeVertexCache.end()) return it->second;
        float d0 = distances[idx0];
        float d1 = distances[idx1];
        float t = glm::clamp(d0 / (d0 - d1), 0.0f, 1.0f);
        uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
        result.vertices.push_back(interpolateVertex(vertices[idx0], vertices[idx1], t));
        edgeVertexCache[key] = newIndex;
        return newIndex;
    }

    ClipResult MeshClipper::clipToHalfSpace(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const glm::vec4& plane)
    {
        ClipResult result;
        result.didClip = false;

        if (indices.size() < 3)
        {
            return result;
        }

        std::vector<float> distances(vertices.size());
        bool allPositive = true;
        bool allNegative = true;

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            distances[i] = signedDistance(vertices[i].position, plane);
            if (distances[i] < -EPSILON) allPositive = false;
            if (distances[i] > EPSILON) allNegative = false;
        }

        if (allPositive)
        {
            result.vertices = vertices;
            result.indices = indices;
            return result;
        }

        if (allNegative)
        {
            result.didClip = true;
            return result;
        }

        result.didClip = true;
        ClipContext ctx{result, vertices, distances, {}, {}};
        processTriangles(ctx, indices);
        return result;
    }

    void MeshClipper::processTriangles(
        ClipContext& ctx,
        const std::vector<uint32_t>& indices)
    {
        size_t vertCount = ctx.vertices.size();
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
            if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) continue;
            bool p0 = ctx.distances[i0] > -EPSILON;
            bool p1 = ctx.distances[i1] > -EPSILON;
            bool p2 = ctx.distances[i2] > -EPSILON;
            int positiveCount = (p0 ? 1 : 0) + (p1 ? 1 : 0) + (p2 ? 1 : 0);

            if (positiveCount == 3)
            {
                ctx.result.indices.push_back(ctx.getVertexIndex(i0));
                ctx.result.indices.push_back(ctx.getVertexIndex(i1));
                ctx.result.indices.push_back(ctx.getVertexIndex(i2));
            }
            else if (positiveCount == 1)
            {
                clipTriangleOnePositive(ctx, i0, i1, i2, p0, p1, p2);
            }
            else if (positiveCount == 2)
            {
                clipTriangleTwoPositive(ctx, i0, i1, i2, p0, p1, p2);
            }
        }
    }

    void MeshClipper::clipTriangleOnePositive(
        ClipContext& ctx,
        uint32_t i0, uint32_t i1, uint32_t i2,
        bool p0, bool p1, bool p2)
    {
        uint32_t posIdx, negIdx1, negIdx2;
        if (p0)      { posIdx = i0; negIdx1 = i1; negIdx2 = i2; }
        else if (p1) { posIdx = i1; negIdx1 = i2; negIdx2 = i0; }
        else         { posIdx = i2; negIdx1 = i0; negIdx2 = i1; }

        uint32_t newPos = ctx.getVertexIndex(posIdx);
        uint32_t newEdge1 = ctx.getEdgeVertex(posIdx, negIdx1);
        uint32_t newEdge2 = ctx.getEdgeVertex(posIdx, negIdx2);

        ctx.result.indices.push_back(newPos);
        ctx.result.indices.push_back(newEdge1);
        ctx.result.indices.push_back(newEdge2);

        ctx.result.cutEdges.emplace_back(newEdge1, newEdge2);
    }

    void MeshClipper::clipTriangleTwoPositive(
        ClipContext& ctx,
        uint32_t i0, uint32_t i1, uint32_t i2,
        bool p0, bool p1, bool p2)
    {
        uint32_t negIdx, posIdx1, posIdx2;
        if (!p0)      { negIdx = i0; posIdx1 = i1; posIdx2 = i2; }
        else if (!p1) { negIdx = i1; posIdx1 = i2; posIdx2 = i0; }
        else          { negIdx = i2; posIdx1 = i0; posIdx2 = i1; }

        uint32_t newPos1 = ctx.getVertexIndex(posIdx1);
        uint32_t newPos2 = ctx.getVertexIndex(posIdx2);
        uint32_t newEdge1 = ctx.getEdgeVertex(posIdx1, negIdx);
        uint32_t newEdge2 = ctx.getEdgeVertex(posIdx2, negIdx);

        ctx.result.indices.push_back(newPos1);
        ctx.result.indices.push_back(newEdge1);
        ctx.result.indices.push_back(newPos2);

        ctx.result.indices.push_back(newPos2);
        ctx.result.indices.push_back(newEdge1);
        ctx.result.indices.push_back(newEdge2);

        ctx.result.cutEdges.emplace_back(newEdge1, newEdge2);
    }
}
