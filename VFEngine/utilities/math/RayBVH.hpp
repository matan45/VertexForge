#pragma once
#include "BVH.hpp"
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <optional>
#include <stack>

namespace math
{
    // Result of a ray-triangle intersection test
    struct RayTriangleHit
    {
        float t;           // Distance along ray
        float u;           // Barycentric coordinate
        float v;           // Barycentric coordinate
    };

    // Moller-Trumbore ray-triangle intersection
    // Returns hit with distance and barycentric coordinates, or nullopt on miss
    inline std::optional<RayTriangleHit> rayTriangleIntersect(
        const Ray& ray,
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        float maxDist = std::numeric_limits<float>::max())
    {
        constexpr float EPSILON = 1e-7f;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;

        glm::vec3 h = glm::cross(ray.direction, edge2);
        float a = glm::dot(edge1, h);

        // Ray is parallel to triangle
        if (a > -EPSILON && a < EPSILON)
        {
            return std::nullopt;
        }

        float f = 1.0f / a;
        glm::vec3 s = ray.origin - v0;
        float u = f * glm::dot(s, h);

        if (u < 0.0f || u > 1.0f)
        {
            return std::nullopt;
        }

        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(ray.direction, q);

        if (v < 0.0f || u + v > 1.0f)
        {
            return std::nullopt;
        }

        float t = f * glm::dot(edge2, q);

        if (t > EPSILON && t < maxDist)
        {
            return RayTriangleHit{t, u, v};
        }

        return std::nullopt;
    }

    // Triangle primitive for ray-BVH intersection
    struct RayBVHTriangle
    {
        glm::vec3 v0, v1, v2;    // World-space vertex positions
        glm::vec3 n0, n1, n2;    // Per-vertex normals
        glm::vec2 uv0, uv1, uv2; // Per-vertex UVs
        uint32_t entityId;
        uint32_t submeshIdx;

        AABB computeAABB() const
        {
            AABB aabb;
            aabb.min = glm::min(glm::min(v0, v1), v2);
            aabb.max = glm::max(glm::max(v0, v1), v2);
            return aabb;
        }

        glm::vec3 getCenter() const
        {
            return (v0 + v1 + v2) / 3.0f;
        }

        // Interpolate normal at barycentric coordinates
        glm::vec3 interpolateNormal(float u, float v) const
        {
            float w = 1.0f - u - v;
            return glm::normalize(w * n0 + u * n1 + v * n2);
        }

        // Interpolate UV at barycentric coordinates
        glm::vec2 interpolateUV(float u, float v) const
        {
            float w = 1.0f - u - v;
            return w * uv0 + u * uv1 + v * uv2;
        }

        // Interpolate world position at barycentric coordinates
        glm::vec3 interpolatePosition(float u, float v) const
        {
            float w = 1.0f - u - v;
            return w * v0 + u * v1 + v * v2;
        }

        float computeArea() const
        {
            return 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
        }
    };

    // Full hit result from RayBVH query
    struct RayHitResult
    {
        float distance;           // Distance along ray to hit point
        float u, v;               // Barycentric coordinates
        glm::vec3 position;       // World-space hit position
        glm::vec3 normal;         // Interpolated surface normal
        glm::vec2 texCoord;       // Interpolated texture coordinate
        uint32_t entityId;
        uint32_t submeshIdx;
        uint32_t triangleIdx;     // Index into the triangle array
    };

    // BVH optimized for ray queries against triangle meshes
    // Uses iterative traversal with an explicit stack for performance
    class RayBVH
    {
    public:
        RayBVH() = default;

        void build(std::vector<RayBVHTriangle>&& triangles)
        {
            triangles_ = std::move(triangles);
            nodes_.clear();

            if (triangles_.empty())
            {
                return;
            }

            // Build primitive AABBs and indices
            std::vector<uint32_t> indices(triangles_.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(triangles_.size()); ++i)
            {
                indices[i] = i;
            }

            // Precompute all triangle AABBs
            triAABBs_.resize(triangles_.size());
            for (size_t i = 0; i < triangles_.size(); ++i)
            {
                triAABBs_[i] = triangles_[i].computeAABB();
            }

            // Reserve estimated node count
            nodes_.reserve(triangles_.size() * 2);
            nodes_.push_back(BVHNode{});

            buildRecursive(0, indices, 0, static_cast<uint32_t>(indices.size()));

            // Store the reordered triangle indices for leaf access
            triIndices_ = std::move(indices);
        }

        // Find the closest ray-triangle intersection
        // Returns the hit result or nullopt if no intersection found
        std::optional<RayHitResult> queryRay(
            const Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const
        {
            if (nodes_.empty())
            {
                return std::nullopt;
            }

            std::optional<RayHitResult> closestHit;
            float closestDist = maxDist;

            // Iterative traversal with explicit stack
            std::stack<uint32_t> stack;
            stack.push(0);

            while (!stack.empty())
            {
                uint32_t nodeIdx = stack.top();
                stack.pop();

                const BVHNode& node = nodes_[nodeIdx];

                // Test ray against node AABB
                auto aabbHit = node.bounds.intersectRay(ray);
                if (!aabbHit.has_value() || aabbHit.value() > closestDist)
                {
                    continue;
                }

                if (node.isLeaf())
                {
                    // Test ray against each triangle in the leaf
                    for (uint32_t i = 0; i < node.leafCount; ++i)
                    {
                        uint32_t triIdx = triIndices_[node.firstChild + i];
                        const auto& tri = triangles_[triIdx];

                        auto hit = rayTriangleIntersect(ray, tri.v0, tri.v1, tri.v2, closestDist);
                        if (hit.has_value())
                        {
                            closestDist = hit->t;
                            closestHit = RayHitResult{
                                .distance = hit->t,
                                .u = hit->u,
                                .v = hit->v,
                                .position = tri.interpolatePosition(hit->u, hit->v),
                                .normal = tri.interpolateNormal(hit->u, hit->v),
                                .texCoord = tri.interpolateUV(hit->u, hit->v),
                                .entityId = tri.entityId,
                                .submeshIdx = tri.submeshIdx,
                                .triangleIdx = triIdx
                            };
                        }
                    }
                }
                else
                {
                    // Push children — push far child first so near child is processed first
                    uint32_t left = node.firstChild;
                    uint32_t right = node.firstChild + 1;

                    auto leftHit = nodes_[left].bounds.intersectRay(ray);
                    auto rightHit = nodes_[right].bounds.intersectRay(ray);

                    bool hitLeft = leftHit.has_value() && leftHit.value() <= closestDist;
                    bool hitRight = rightHit.has_value() && rightHit.value() <= closestDist;

                    if (hitLeft && hitRight)
                    {
                        // Push far child first (processed last), near child second (processed first)
                        if (leftHit.value() < rightHit.value())
                        {
                            stack.push(right);
                            stack.push(left);
                        }
                        else
                        {
                            stack.push(left);
                            stack.push(right);
                        }
                    }
                    else if (hitLeft)
                    {
                        stack.push(left);
                    }
                    else if (hitRight)
                    {
                        stack.push(right);
                    }
                }
            }

            return closestHit;
        }

        // Shadow/occlusion ray — returns true if ANY triangle is hit within maxDist
        // Early-exits on first hit for performance
        bool queryOcclusion(
            const Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const
        {
            if (nodes_.empty())
            {
                return false;
            }

            std::stack<uint32_t> stack;
            stack.push(0);

            while (!stack.empty())
            {
                uint32_t nodeIdx = stack.top();
                stack.pop();

                const BVHNode& node = nodes_[nodeIdx];

                auto aabbHit = node.bounds.intersectRay(ray);
                if (!aabbHit.has_value() || aabbHit.value() > maxDist)
                {
                    continue;
                }

                if (node.isLeaf())
                {
                    for (uint32_t i = 0; i < node.leafCount; ++i)
                    {
                        uint32_t triIdx = triIndices_[node.firstChild + i];
                        const auto& tri = triangles_[triIdx];

                        auto hit = rayTriangleIntersect(ray, tri.v0, tri.v1, tri.v2, maxDist);
                        if (hit.has_value())
                        {
                            return true; // Early exit — any hit is enough
                        }
                    }
                }
                else
                {
                    stack.push(node.firstChild);
                    stack.push(node.firstChild + 1);
                }
            }

            return false;
        }

        // Accessors
        size_t getTriangleCount() const { return triangles_.size(); }
        size_t getNodeCount() const { return nodes_.size(); }
        bool isBuilt() const { return !nodes_.empty(); }

        const RayBVHTriangle& getTriangle(uint32_t idx) const { return triangles_[idx]; }
        const std::vector<RayBVHTriangle>& getTriangles() const { return triangles_; }

        void clear()
        {
            nodes_.clear();
            triangles_.clear();
            triAABBs_.clear();
            triIndices_.clear();
        }

    private:
        std::vector<BVHNode> nodes_;
        std::vector<RayBVHTriangle> triangles_;
        std::vector<AABB> triAABBs_;
        std::vector<uint32_t> triIndices_;   // Reordered triangle indices after build

        void buildRecursive(uint32_t nodeIdx, std::vector<uint32_t>& indices,
                            uint32_t start, uint32_t end)
        {
            BVHNode& node = nodes_[nodeIdx];

            // Compute bounds for this node
            node.bounds = computeBounds(indices, start, end);

            uint32_t primCount = end - start;
            constexpr uint32_t maxLeafSize = 4;

            if (primCount <= maxLeafSize)
            {
                // Leaf node
                node.firstChild = start;
                node.leafCount = primCount;
                return;
            }

            // Find the split axis (longest extent)
            glm::vec3 extent = node.bounds.max - node.bounds.min;
            int axis = 0;
            if (extent.y > extent.x) axis = 1;
            if (extent.z > extent[axis]) axis = 2;

            // Median split
            uint32_t mid = start + primCount / 2;

            std::nth_element(
                indices.begin() + start,
                indices.begin() + mid,
                indices.begin() + end,
                [this, axis](uint32_t a, uint32_t b)
                {
                    return triangles_[a].getCenter()[axis] < triangles_[b].getCenter()[axis];
                }
            );

            // Create child nodes
            uint32_t leftChild = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(BVHNode{});
            nodes_.push_back(BVHNode{});

            // Re-fetch node reference (vector may have reallocated)
            nodes_[nodeIdx].firstChild = leftChild;
            nodes_[nodeIdx].leafCount = 0;

            buildRecursive(leftChild, indices, start, mid);
            buildRecursive(leftChild + 1, indices, mid, end);
        }

        AABB computeBounds(const std::vector<uint32_t>& indices, uint32_t start, uint32_t end) const
        {
            AABB bounds;
            bounds.min = glm::vec3(std::numeric_limits<float>::max());
            bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

            for (uint32_t i = start; i < end; ++i)
            {
                const auto& aabb = triAABBs_[indices[i]];
                bounds.min = glm::min(bounds.min, aabb.min);
                bounds.max = glm::max(bounds.max, aabb.max);
            }

            return bounds;
        }
    };
}
