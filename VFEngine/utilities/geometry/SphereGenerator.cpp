#include "SphereGenerator.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace geometry
{
    resource::MeshData SphereGenerator::generate(const SphereParams& params)
    {
        resource::MeshData mesh;
        mesh.name = "PreviewSphere";

        mesh.lodLevels.resize(1);
        auto& lod0 = mesh.lodLevels[0];

        const float radius = params.radius;
        const uint32_t latSegs = params.latitudeSegments;
        const uint32_t lonSegs = params.longitudeSegments;

        // Generate vertices
        for (uint32_t lat = 0; lat <= latSegs; ++lat)
        {
            float theta = static_cast<float>(lat) * glm::pi<float>() / static_cast<float>(latSegs);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (uint32_t lon = 0; lon <= lonSegs; ++lon)
            {
                float phi = static_cast<float>(lon) * 2.0f * glm::pi<float>() / static_cast<float>(lonSegs);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                resource::Vertex vertex;

                // Normal is the same as position on a unit sphere
                vertex.normal = glm::vec3(
                    sinTheta * cosPhi,
                    cosTheta,
                    sinTheta * sinPhi
                );

                // Position scaled by radius
                vertex.position = vertex.normal * radius;

                // UV coordinates
                vertex.texCoords = glm::vec2(
                    static_cast<float>(lon) / static_cast<float>(lonSegs),
                    static_cast<float>(lat) / static_cast<float>(latSegs)
                );

                lod0.vertices.push_back(vertex);
            }
        }

        // Generate indices with correct CCW winding for outward-facing normals
        // Vulkan uses CCW as front face (configured in pipeline)
        for (uint32_t lat = 0; lat < latSegs; ++lat)
        {
            for (uint32_t lon = 0; lon < lonSegs; ++lon)
            {
                uint32_t topLeft = lat * (lonSegs + 1) + lon;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = topLeft + lonSegs + 1;
                uint32_t bottomRight = bottomLeft + 1;
                
                if (lat != 0)
                {
                    // Upper-left triangle of quad: CCW from outside
                    lod0.indices.push_back(topLeft);
                    lod0.indices.push_back(bottomLeft);
                    lod0.indices.push_back(topRight);
                }

                if (lat != latSegs - 1)
                {
                    // Lower-right triangle of quad: CCW from outside
                    lod0.indices.push_back(topRight);
                    lod0.indices.push_back(bottomLeft);
                    lod0.indices.push_back(bottomRight);
                }
            }
        }

        return mesh;
    }

    resource::MeshesData SphereGenerator::generateMeshesData(const SphereParams& params)
    {
        resource::MeshesData meshesData;
        meshesData.headerFileType = resource::FileType::MESH;
        meshesData.numberOfMeshes = 1;
        meshesData.meshes.push_back(generate(params));
        return meshesData;
    }
}
