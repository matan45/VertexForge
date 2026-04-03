#include "MeshClipper.hpp"
#include <unordered_map>
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
        result.normal = glm::normalize(glm::mix(a.normal, b.normal, t));
        result.texCoords = glm::mix(a.texCoords, b.texCoords, t);
        result.boneIndices = a.boneIndices;
        result.boneWeights = glm::mix(a.boneWeights, b.boneWeights, t);
        return result;
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

        // Classify all vertices
        std::vector<float> distances(vertices.size());
        bool allPositive = true;
        bool allNegative = true;

        for (size_t i = 0; i < vertices.size(); ++i)
        {
            distances[i] = signedDistance(vertices[i].position, plane);
            if (distances[i] < -EPSILON) allPositive = false;
            if (distances[i] > EPSILON) allNegative = false;
        }

        // All vertices on positive side - no clipping needed
        if (allPositive)
        {
            result.vertices = vertices;
            result.indices = indices;
            return result;
        }

        // All vertices on negative side - mesh fully clipped away
        if (allNegative)
        {
            result.didClip = true;
            return result;
        }

        result.didClip = true;

        // Map from old vertex index to new vertex index for vertices kept as-is
        std::unordered_map<uint32_t, uint32_t> vertexMap;

        // Cache for interpolated vertices on edges to avoid duplicates
        // Key: pack two vertex indices (smaller first) into uint64_t
        auto edgeKey = [](uint32_t a, uint32_t b) -> uint64_t
        {
            if (a > b) std::swap(a, b);
            return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
        };
        std::unordered_map<uint64_t, uint32_t> edgeVertexCache;

        // Helper to get or create a vertex index in the output
        auto getVertexIndex = [&](uint32_t oldIndex) -> uint32_t
        {
            auto it = vertexMap.find(oldIndex);
            if (it != vertexMap.end()) return it->second;

            uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(vertices[oldIndex]);
            vertexMap[oldIndex] = newIndex;
            return newIndex;
        };

        // Helper to get or create an interpolated vertex on an edge
        auto getEdgeVertex = [&](uint32_t idx0, uint32_t idx1) -> uint32_t
        {
            uint64_t key = edgeKey(idx0, idx1);
            auto it = edgeVertexCache.find(key);
            if (it != edgeVertexCache.end()) return it->second;

            float d0 = distances[idx0];
            float d1 = distances[idx1];
            float t = d0 / (d0 - d1);
            t = glm::clamp(t, 0.0f, 1.0f);

            uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(interpolateVertex(vertices[idx0], vertices[idx1], t));
            edgeVertexCache[key] = newIndex;
            return newIndex;
        };

        // Process each triangle
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            float d0 = distances[i0];
            float d1 = distances[i1];
            float d2 = distances[i2];

            bool p0 = d0 > -EPSILON;
            bool p1 = d1 > -EPSILON;
            bool p2 = d2 > -EPSILON;

            int positiveCount = (p0 ? 1 : 0) + (p1 ? 1 : 0) + (p2 ? 1 : 0);

            if (positiveCount == 3)
            {
                // All positive - keep triangle
                result.indices.push_back(getVertexIndex(i0));
                result.indices.push_back(getVertexIndex(i1));
                result.indices.push_back(getVertexIndex(i2));
            }
            else if (positiveCount == 0)
            {
                // All negative - discard
                continue;
            }
            else if (positiveCount == 1)
            {
                // One vertex positive, two negative -> one triangle
                // Rotate so that the positive vertex is first
                uint32_t posIdx, negIdx1, negIdx2;
                if (p0)
                {
                    posIdx = i0; negIdx1 = i1; negIdx2 = i2;
                }
                else if (p1)
                {
                    posIdx = i1; negIdx1 = i2; negIdx2 = i0;
                }
                else
                {
                    posIdx = i2; negIdx1 = i0; negIdx2 = i1;
                }

                uint32_t newPos = getVertexIndex(posIdx);
                uint32_t newEdge1 = getEdgeVertex(posIdx, negIdx1);
                uint32_t newEdge2 = getEdgeVertex(posIdx, negIdx2);

                result.indices.push_back(newPos);
                result.indices.push_back(newEdge1);
                result.indices.push_back(newEdge2);

                // Track cut edge (the edge between the two new interpolated vertices)
                result.cutEdges.emplace_back(newEdge1, newEdge2);
            }
            else // positiveCount == 2
            {
                // Two vertices positive, one negative -> two triangles (quad)
                // Rotate so that the negative vertex is first
                uint32_t negIdx, posIdx1, posIdx2;
                if (!p0)
                {
                    negIdx = i0; posIdx1 = i1; posIdx2 = i2;
                }
                else if (!p1)
                {
                    negIdx = i1; posIdx1 = i2; posIdx2 = i0;
                }
                else
                {
                    negIdx = i2; posIdx1 = i0; posIdx2 = i1;
                }

                uint32_t newPos1 = getVertexIndex(posIdx1);
                uint32_t newPos2 = getVertexIndex(posIdx2);
                uint32_t newEdge1 = getEdgeVertex(posIdx1, negIdx);
                uint32_t newEdge2 = getEdgeVertex(posIdx2, negIdx);

                // Triangle 1: posIdx1, newEdge1, posIdx2
                result.indices.push_back(newPos1);
                result.indices.push_back(newEdge1);
                result.indices.push_back(newPos2);

                // Triangle 2: posIdx2, newEdge1, newEdge2
                result.indices.push_back(newPos2);
                result.indices.push_back(newEdge1);
                result.indices.push_back(newEdge2);

                // Track cut edge
                result.cutEdges.emplace_back(newEdge1, newEdge2);
            }
        }

        return result;
    }
}
