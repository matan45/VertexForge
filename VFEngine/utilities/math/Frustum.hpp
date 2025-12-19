#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace math
{
    struct Ray
    {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};

        Ray() = default;

        Ray(const glm::vec3& o, const glm::vec3& d) : origin(o), direction(glm::normalize(d))
        {
        }
    };

    struct AABB
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        AABB() = default;

        AABB(const glm::vec3& minPoint, const glm::vec3& maxPoint)
            : min(minPoint), max(maxPoint)
        {
        }

        glm::vec3 getCenter() const
        {
            return (min + max) * 0.5f;
        }

        glm::vec3 getExtents() const
        {
            return (max - min) * 0.5f;
        }

        bool isValid() const
        {
            return min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        void expand(const glm::vec3& point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        // Transform AABB by a matrix and return new AABB
        // Uses Arvo's algorithm for optimal performance (1 transform instead of 8)
        AABB getTransformed(const glm::mat4& m) const
        {
            glm::vec3 center = getCenter();
            glm::vec3 extents = getExtents();

            // Transform center point
            glm::vec3 newCenter = glm::vec3(m * glm::vec4(center, 1.0f));

            // Compute new extents using absolute values of the rotation/scale matrix
            // Each new axis extent is the sum of contributions from all original axes
            glm::vec3 newExtents(
                std::abs(m[0][0]) * extents.x + std::abs(m[1][0]) * extents.y + std::abs(m[2][0]) * extents.z,
                std::abs(m[0][1]) * extents.x + std::abs(m[1][1]) * extents.y + std::abs(m[2][1]) * extents.z,
                std::abs(m[0][2]) * extents.x + std::abs(m[1][2]) * extents.y + std::abs(m[2][2]) * extents.z
            );

            return AABB(newCenter - newExtents, newCenter + newExtents);
        }

        // Ray-AABB intersection test (slab method)
        std::optional<float> intersectRay(const Ray& ray) const
        {
            float tmin = 0.0f;
            float tmax = std::numeric_limits<float>::max();

            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(ray.direction[i]) < 1e-6f)
                {
                    // Ray is parallel to slab, check if origin is within slab
                    if (ray.origin[i] < min[i] || ray.origin[i] > max[i])
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    float invD = 1.0f / ray.direction[i];
                    float t1 = (min[i] - ray.origin[i]) * invD;
                    float t2 = (max[i] - ray.origin[i]) * invD;

                    if (t1 > t2) std::swap(t1, t2);

                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);

                    if (tmin > tmax)
                    {
                        return std::nullopt;
                    }
                }
            }

            return tmin;
        }
    };

    // Camera frustum for visibility culling
    class Frustum
    {
    public:
        // Frustum planes: Left, Right, Bottom, Top, Near, Far
        enum Plane { Left = 0, Right, Bottom, Top, Near, Far, Count };
        
    private:
        std::array<glm::vec4, Count> planes;
        bool initialized = false;
        
    public:
        

        Frustum() = default;
        
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

   
    };
}
