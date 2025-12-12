#pragma once
#include <glm/glm.hpp>
#include <array>

namespace math
{
    // Axis-Aligned Bounding Box
    struct AABB
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        AABB() = default;
        AABB(const glm::vec3& minPoint, const glm::vec3& maxPoint)
            : min(minPoint), max(maxPoint) {}

        // Get center of the AABB
        glm::vec3 getCenter() const
        {
            return (min + max) * 0.5f;
        }

        // Get half-extents (size from center to edge)
        glm::vec3 getExtents() const
        {
            return (max - min) * 0.5f;
        }

        // Check if AABB is valid (min <= max)
        bool isValid() const
        {
            return min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        // Expand AABB to include a point
        void expand(const glm::vec3& point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        // Transform AABB by a matrix and return new AABB
        AABB getTransformed(const glm::mat4& matrix) const
        {
            // Get all 8 corners of the AABB
            glm::vec3 corners[8] = {
                glm::vec3(min.x, min.y, min.z),
                glm::vec3(max.x, min.y, min.z),
                glm::vec3(min.x, max.y, min.z),
                glm::vec3(max.x, max.y, min.z),
                glm::vec3(min.x, min.y, max.z),
                glm::vec3(max.x, min.y, max.z),
                glm::vec3(min.x, max.y, max.z),
                glm::vec3(max.x, max.y, max.z)
            };

            // Transform first corner to initialize new AABB
            glm::vec4 transformed = matrix * glm::vec4(corners[0], 1.0f);
            AABB result{glm::vec3(transformed), glm::vec3(transformed)};

            // Expand to include all other transformed corners
            for (int i = 1; i < 8; ++i)
            {
                transformed = matrix * glm::vec4(corners[i], 1.0f);
                result.expand(glm::vec3(transformed));
            }

            return result;
        }
    };

    // Camera frustum for visibility culling
    class Frustum
    {
    public:
        // Frustum planes: Left, Right, Bottom, Top, Near, Far
        enum Plane { Left = 0, Right, Bottom, Top, Near, Far, Count };

        Frustum() = default;

        // Check if frustum has been initialized with valid planes
        bool isInitialized() const { return initialized; }

        // Extract frustum planes from view-projection matrix
        void extractFromMatrix(const glm::mat4& viewProjection)
        {
            // Extract frustum planes using Gribb/Hartmann method
            // Each plane is stored as (A, B, C, D) where Ax + By + Cz + D = 0

            // Left plane
            planes[Left] = glm::vec4(
                viewProjection[0][3] + viewProjection[0][0],
                viewProjection[1][3] + viewProjection[1][0],
                viewProjection[2][3] + viewProjection[2][0],
                viewProjection[3][3] + viewProjection[3][0]
            );

            // Right plane
            planes[Right] = glm::vec4(
                viewProjection[0][3] - viewProjection[0][0],
                viewProjection[1][3] - viewProjection[1][0],
                viewProjection[2][3] - viewProjection[2][0],
                viewProjection[3][3] - viewProjection[3][0]
            );

            // Bottom plane
            planes[Bottom] = glm::vec4(
                viewProjection[0][3] + viewProjection[0][1],
                viewProjection[1][3] + viewProjection[1][1],
                viewProjection[2][3] + viewProjection[2][1],
                viewProjection[3][3] + viewProjection[3][1]
            );

            // Top plane
            planes[Top] = glm::vec4(
                viewProjection[0][3] - viewProjection[0][1],
                viewProjection[1][3] - viewProjection[1][1],
                viewProjection[2][3] - viewProjection[2][1],
                viewProjection[3][3] - viewProjection[3][1]
            );

            // Near plane
            planes[Near] = glm::vec4(
                viewProjection[0][3] + viewProjection[0][2],
                viewProjection[1][3] + viewProjection[1][2],
                viewProjection[2][3] + viewProjection[2][2],
                viewProjection[3][3] + viewProjection[3][2]
            );

            // Far plane
            planes[Far] = glm::vec4(
                viewProjection[0][3] - viewProjection[0][2],
                viewProjection[1][3] - viewProjection[1][2],
                viewProjection[2][3] - viewProjection[2][2],
                viewProjection[3][3] - viewProjection[3][2]
            );

            // Normalize all planes
            for (auto& plane : planes)
            {
                float length = glm::length(glm::vec3(plane));
                if (length > 0.0f)
                {
                    plane /= length;
                }
            }

            initialized = true;
        }

        // Test if an AABB intersects with the frustum
        // Returns true if the AABB is at least partially inside the frustum
        bool intersectsAABB(const AABB& aabb) const
        {
            for (const auto& plane : planes)
            {
                glm::vec3 normal(plane);

                // Find the positive vertex (furthest along the plane normal)
                glm::vec3 positiveVertex = aabb.min;
                if (normal.x >= 0.0f) positiveVertex.x = aabb.max.x;
                if (normal.y >= 0.0f) positiveVertex.y = aabb.max.y;
                if (normal.z >= 0.0f) positiveVertex.z = aabb.max.z;

                // If the positive vertex is outside this plane, the AABB is outside the frustum
                if (glm::dot(normal, positiveVertex) + plane.w < 0.0f)
                {
                    return false;
                }
            }

            return true;
        }

        // Test if an AABB (with model transform) intersects with the frustum
        bool intersectsAABB(const AABB& localAABB, const glm::mat4& modelMatrix) const
        {
            // Transform AABB to world space
            AABB worldAABB = localAABB.getTransformed(modelMatrix);
            return intersectsAABB(worldAABB);
        }

    private:
        std::array<glm::vec4, Count> planes;
        bool initialized = false;
    };
}
