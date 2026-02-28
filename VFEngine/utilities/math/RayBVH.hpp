#pragma once
#include "BVH.hpp"
#include <vector>
#include <cstdint>
#include <optional>

namespace math
{
    struct RayTriangleHit
    {
        float t;
        float u;
        float v;
    };

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

    struct RayBVHTriangle
    {
        glm::vec3 v0, v1, v2;
        glm::vec3 n0, n1, n2;
        glm::vec2 uv0, uv1, uv2;
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

        glm::vec3 interpolateNormal(float u, float v) const
        {
            float w = 1.0f - u - v;
            return glm::normalize(w * n0 + u * n1 + v * n2);
        }

        glm::vec2 interpolateUV(float u, float v) const
        {
            float w = 1.0f - u - v;
            return w * uv0 + u * uv1 + v * uv2;
        }

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

    struct RayHitResult
    {
        float distance;
        float u, v;
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoord;
        uint32_t entityId;
        uint32_t submeshIdx;
        uint32_t triangleIdx;
    };

    class RayBVH
    {
    private:
        std::vector<BVHNode> nodes;
        std::vector<RayBVHTriangle> triangles;
        std::vector<AABB> triAABBs;
        std::vector<uint32_t> triIndices;

    public:
        RayBVH() = default;

        void build(std::vector<RayBVHTriangle>&& triangles);

        std::optional<RayHitResult> queryRay(
            const Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        bool queryOcclusion(
            const Ray& ray,
            float maxDist = std::numeric_limits<float>::max()) const;

        size_t getTriangleCount() const { return triangles.size(); }
        size_t getNodeCount() const { return nodes.size(); }
        bool isBuilt() const { return !nodes.empty(); }

        const RayBVHTriangle& getTriangle(uint32_t idx) const { return triangles[idx]; }
        const std::vector<RayBVHTriangle>& getTriangles() const { return triangles; }

        void clear();

    private:
        void buildRecursive(uint32_t nodeIdx, std::vector<uint32_t>& indices,
                            uint32_t start, uint32_t end);

        AABB computeBounds(const std::vector<uint32_t>& indices, uint32_t start, uint32_t end) const;
    };
}
