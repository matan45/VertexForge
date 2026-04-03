#include <VHACD.h>

#include "ConvexHullGenerator.hpp"
#include <algorithm>

namespace destruction
{
    resource::ConvexHull ConvexHullGenerator::generate(
        const std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        const FragmentHullConfig& config)
    {
        resource::ConvexHull result;

        if (vertices.empty() || indices.size() < 3)
        {
            return result;
        }

        // Convert vertex positions to double precision for V-HACD
        std::vector<double> points;
        points.reserve(vertices.size() * 3);
        for (const auto& v : vertices)
        {
            points.push_back(static_cast<double>(v.position.x));
            points.push_back(static_cast<double>(v.position.y));
            points.push_back(static_cast<double>(v.position.z));
        }

        // Configure V-HACD for single hull output (fragments are nearly convex)
        VHACD::IVHACD::Parameters params;
        params.m_maxConvexHulls = 1;
        params.m_resolution = config.resolution;
        params.m_maxNumVerticesPerCH = config.maxVerticesPerHull;
        params.m_shrinkWrap = config.shrinkWrap;

        VHACD::IVHACD* vhacd = VHACD::CreateVHACD();

        bool success = vhacd->Compute(
            points.data(),
            static_cast<uint32_t>(vertices.size()),
            indices.data(),
            static_cast<uint32_t>(indices.size() / 3),
            params);

        if (success && vhacd->GetNConvexHulls() > 0)
        {
            VHACD::IVHACD::ConvexHull hull;
            vhacd->GetConvexHull(0, hull);

            result.vertices.reserve(hull.m_points.size());
            for (const auto& p : hull.m_points)
            {
                result.vertices.emplace_back(
                    static_cast<float>(p.mX),
                    static_cast<float>(p.mY),
                    static_cast<float>(p.mZ));
            }

            result.indices.reserve(hull.m_triangles.size() * 3);
            for (const auto& tri : hull.m_triangles)
            {
                result.indices.push_back(tri.mI0);
                result.indices.push_back(tri.mI1);
                result.indices.push_back(tri.mI2);
            }

            result.center = glm::vec3(
                static_cast<float>(hull.m_center.GetX()),
                static_cast<float>(hull.m_center.GetY()),
                static_cast<float>(hull.m_center.GetZ()));
            result.volume = static_cast<float>(hull.m_volume);
        }

        vhacd->Release();
        return result;
    }

    void ConvexHullGenerator::generateBatch(
        std::vector<FragmentData>& fragments,
        const FragmentHullConfig& config)
    {
        for (auto& fragment : fragments)
        {
            if (fragment.mesh.lodLevels.empty())
                continue;

            const auto& lod0 = fragment.mesh.lodLevels[0];
            fragment.colliderHull = generate(lod0.vertices, lod0.indices, config);
        }
    }
}
