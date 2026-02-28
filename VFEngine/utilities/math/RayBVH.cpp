#include "RayBVH.hpp"
#include <algorithm>
#include <limits>
#include <stack>

namespace math
{
    void RayBVH::build(std::vector<RayBVHTriangle>&& tris)
    {
        triangles = std::move(tris);
        nodes.clear();

        if (triangles.empty())
        {
            return;
        }

        std::vector<uint32_t> indices(triangles.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(triangles.size()); ++i)
        {
            indices[i] = i;
        }

        triAABBs.resize(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i)
        {
            triAABBs[i] = triangles[i].computeAABB();
        }

        nodes.reserve(triangles.size() * 2);
        nodes.push_back(BVHNode{});

        buildRecursive(0, indices, 0, static_cast<uint32_t>(indices.size()));

        triIndices = std::move(indices);
    }

    std::optional<RayHitResult> RayBVH::queryRay(
        const Ray& ray,
        float maxDist) const
    {
        if (nodes.empty())
        {
            return std::nullopt;
        }

        std::optional<RayHitResult> closestHit;
        float closestDist = maxDist;

        std::stack<uint32_t> stack;
        stack.push(0);

        while (!stack.empty())
        {
            uint32_t nodeIdx = stack.top();
            stack.pop();

            const BVHNode& node = nodes[nodeIdx];

            auto aabbHit = node.bounds.intersectRay(ray);
            if (!aabbHit.has_value() || aabbHit.value() > closestDist)
            {
                continue;
            }

            if (node.isLeaf())
            {
                for (uint32_t i = 0; i < node.leafCount; ++i)
                {
                    uint32_t triIdx = triIndices[node.firstChild + i];
                    const auto& tri = triangles[triIdx];

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
                uint32_t left = node.firstChild;
                uint32_t right = node.firstChild + 1;

                auto leftHit = nodes[left].bounds.intersectRay(ray);
                auto rightHit = nodes[right].bounds.intersectRay(ray);

                bool hitLeft = leftHit.has_value() && leftHit.value() <= closestDist;
                bool hitRight = rightHit.has_value() && rightHit.value() <= closestDist;

                if (hitLeft && hitRight)
                {
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

    bool RayBVH::queryOcclusion(
        const Ray& ray,
        float maxDist) const
    {
        if (nodes.empty())
        {
            return false;
        }

        std::stack<uint32_t> stack;
        stack.push(0);

        while (!stack.empty())
        {
            uint32_t nodeIdx = stack.top();
            stack.pop();

            const BVHNode& node = nodes[nodeIdx];

            auto aabbHit = node.bounds.intersectRay(ray);
            if (!aabbHit.has_value() || aabbHit.value() > maxDist)
            {
                continue;
            }

            if (node.isLeaf())
            {
                for (uint32_t i = 0; i < node.leafCount; ++i)
                {
                    uint32_t triIdx = triIndices[node.firstChild + i];
                    const auto& tri = triangles[triIdx];

                    auto hit = rayTriangleIntersect(ray, tri.v0, tri.v1, tri.v2, maxDist);
                    if (hit.has_value())
                    {
                        // Early exit — any hit is enough
                        return true;
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

    void RayBVH::clear()
    {
        nodes.clear();
        triangles.clear();
        triAABBs.clear();
        triIndices.clear();
    }

    void RayBVH::buildRecursive(uint32_t nodeIdx, std::vector<uint32_t>& indices,
                                uint32_t start, uint32_t end)
    {
        BVHNode& node = nodes[nodeIdx];

        node.bounds = computeBounds(indices, start, end);

        uint32_t primCount = end - start;
        constexpr uint32_t maxLeafSize = 4;

        if (primCount <= maxLeafSize)
        {
            node.firstChild = start;
            node.leafCount = primCount;
            return;
        }

        glm::vec3 extent = node.bounds.max - node.bounds.min;
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > extent[axis]) axis = 2;

        uint32_t mid = start + primCount / 2;

        std::nth_element(
            indices.begin() + start,
            indices.begin() + mid,
            indices.begin() + end,
            [this, axis](uint32_t a, uint32_t b)
            {
                return triangles[a].getCenter()[axis] < triangles[b].getCenter()[axis];
            }
        );

        uint32_t leftChild = static_cast<uint32_t>(nodes.size());
        nodes.push_back(BVHNode{});
        nodes.push_back(BVHNode{});

        // Re-fetch after potential reallocation
        nodes[nodeIdx].firstChild = leftChild;
        nodes[nodeIdx].leafCount = 0;

        buildRecursive(leftChild, indices, start, mid);
        buildRecursive(leftChild + 1, indices, mid, end);
    }

    AABB RayBVH::computeBounds(const std::vector<uint32_t>& indices, uint32_t start, uint32_t end) const
    {
        AABB bounds;
        bounds.min = glm::vec3(std::numeric_limits<float>::max());
        bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

        for (uint32_t i = start; i < end; ++i)
        {
            const auto& aabb = triAABBs[indices[i]];
            bounds.min = glm::min(bounds.min, aabb.min);
            bounds.max = glm::max(bounds.max, aabb.max);
        }

        return bounds;
    }
}
