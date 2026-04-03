#pragma once

#include <destruction/DestructionTypes.hpp>
#include <glm/glm.hpp>

namespace
{
    // Build a unit cube mesh centered at origin: [-0.5, 0.5]^3
    // 8 vertices, 12 triangles (2 per face)
    resource::MeshData makeUnitCube()
    {
        resource::MeshData mesh;
        mesh.name = "cube";

        resource::LODLevel lod;

        // 8 corner vertices (normals approximate, UVs simple)
        glm::vec3 corners[8] = {
            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
            { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
            { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}
        };

        // For simplicity, use per-face vertices (24 vertices, 6 faces x 4 verts)
        auto addFace = [&](glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, glm::vec3 normal)
        {
            uint32_t base = static_cast<uint32_t>(lod.vertices.size());
            resource::Vertex verts[4];
            glm::vec3 positions[4] = {v0, v1, v2, v3};
            glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};

            for (int i = 0; i < 4; ++i)
            {
                verts[i].position = positions[i];
                verts[i].normal = normal;
                verts[i].texCoords = uvs[i];
                verts[i].boneIndices = glm::ivec4(-1);
                verts[i].boneWeights = glm::vec4(0.0f);
                lod.vertices.push_back(verts[i]);
            }

            // Two triangles: 0-1-2, 0-2-3
            lod.indices.push_back(base);
            lod.indices.push_back(base + 1);
            lod.indices.push_back(base + 2);
            lod.indices.push_back(base);
            lod.indices.push_back(base + 2);
            lod.indices.push_back(base + 3);
        };

        // Front  (+Z)
        addFace(corners[4], corners[5], corners[6], corners[7], {0,0,1});
        // Back   (-Z)
        addFace(corners[1], corners[0], corners[3], corners[2], {0,0,-1});
        // Right  (+X)
        addFace(corners[5], corners[1], corners[2], corners[6], {1,0,0});
        // Left   (-X)
        addFace(corners[0], corners[4], corners[7], corners[3], {-1,0,0});
        // Top    (+Y)
        addFace(corners[7], corners[6], corners[2], corners[3], {0,1,0});
        // Bottom (-Y)
        addFace(corners[0], corners[1], corners[5], corners[4], {0,-1,0});

        mesh.lodLevels.push_back(std::move(lod));
        return mesh;
    }

    // Build a simple tetrahedron mesh
    resource::MeshData makeTetrahedron()
    {
        resource::MeshData mesh;
        mesh.name = "tetrahedron";

        resource::LODLevel lod;

        glm::vec3 v0(0.0f, 1.0f, 0.0f);
        glm::vec3 v1(-0.5f, 0.0f, 0.5f);
        glm::vec3 v2(0.5f, 0.0f, 0.5f);
        glm::vec3 v3(0.0f, 0.0f, -0.5f);

        auto addTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c)
        {
            uint32_t base = static_cast<uint32_t>(lod.vertices.size());
            glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));

            for (const auto& pos : {a, b, c})
            {
                resource::Vertex vert;
                vert.position = pos;
                vert.normal = normal;
                vert.texCoords = {0.0f, 0.0f};
                vert.boneIndices = glm::ivec4(-1);
                vert.boneWeights = glm::vec4(0.0f);
                lod.vertices.push_back(vert);
            }

            lod.indices.push_back(base);
            lod.indices.push_back(base + 1);
            lod.indices.push_back(base + 2);
        };

        addTri(v0, v1, v2);
        addTri(v0, v2, v3);
        addTri(v0, v3, v1);
        addTri(v1, v3, v2);

        mesh.lodLevels.push_back(std::move(lod));
        return mesh;
    }
}
