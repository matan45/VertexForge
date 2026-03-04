#include "BakeSceneMesh.hpp"
#include "../print/Log.hpp"
#include "../resource/MeshStreamHandle.hpp"
#include "../components/CoreComponents.hpp"
#include "../scene/EntityRegistry.hpp"
#include <glm/gtc/matrix_inverse.hpp>

namespace lightbake
{
    bool BakeSceneMesh::buildFromScene(
        const TerrainBakeGeometry& terrain,
        const std::vector<WaterBakeTile>& waterTiles,
        BakeProgressCallback progressCallback)
    {
        clear();

        auto meshEntities = collectStaticMeshEntities();
        vfLogInfo("[LightBake] Found {} static mesh entities for baking", meshEntities.size());

        std::vector<math::RayBVHTriangle> triangles;
        loadMeshTriangles(meshEntities, triangles, progressCallback);
        addTerrainTriangles(terrain, triangles);
        addWaterTriangles(waterTiles, triangles);

        if (triangles.empty())
        {
            vfLogWarning("[LightBake] No valid triangles found in scene");
            return false;
        }

        vfLogInfo("[LightBake] Building BVH from {} triangles", triangles.size());
        bvh_.build(std::move(triangles));
        vfLogInfo("[LightBake] BVH built: {} nodes", bvh_.getNodeCount());

        return true;
    }

    std::vector<BakeSceneMesh::MeshEntity> BakeSceneMesh::collectStaticMeshEntities()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<MeshEntity> meshEntities;

        auto view = registry.view<components::MeshComponent,
                                  components::TransformComponent,
                                  components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic)
                continue;

            const auto& meshComp = view.get<components::MeshComponent>(entity);
            if (meshComp.meshPath.empty())
                continue;

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            MeshEntity me;
            me.meshPath = meshComp.meshPath;
            me.worldMatrix = worldTransform.worldMatrix;
            me.normalMatrix = glm::mat3(glm::inverseTranspose(worldTransform.worldMatrix));
            me.entityId = static_cast<uint32_t>(entity);
            meshEntities.push_back(std::move(me));
        }

        return meshEntities;
    }

    void BakeSceneMesh::loadMeshTriangles(const std::vector<MeshEntity>& meshEntities,
                                          std::vector<math::RayBVHTriangle>& triangles,
                                          BakeProgressCallback progressCallback)
    {
        size_t totalEntities = meshEntities.size();

        for (size_t entityIdx = 0; entityIdx < totalEntities; ++entityIdx)
        {
            const auto& me = meshEntities[entityIdx];

            auto meshData = resource::MeshStreamResource::loadAll(me.meshPath);
            if (meshData.meshes.empty())
            {
                vfLogWarning("[LightBake] Failed to load mesh: {}", me.meshPath);
                continue;
            }

            for (uint32_t submeshIdx = 0; submeshIdx < static_cast<uint32_t>(meshData.meshes.size()); ++submeshIdx)
            {
                const auto& mesh = meshData.meshes[submeshIdx];
                if (mesh.lodLevels.empty())
                    continue;

                const auto& lod0 = mesh.lodLevels[0];
                if (lod0.vertices.empty() || lod0.indices.empty())
                    continue;

                for (size_t i = 0; i + 2 < lod0.indices.size(); i += 3)
                {
                    const auto& vert0 = lod0.vertices[lod0.indices[i]];
                    const auto& vert1 = lod0.vertices[lod0.indices[i + 1]];
                    const auto& vert2 = lod0.vertices[lod0.indices[i + 2]];

                    math::RayBVHTriangle tri;
                    tri.v0 = glm::vec3(me.worldMatrix * glm::vec4(vert0.position, 1.0f));
                    tri.v1 = glm::vec3(me.worldMatrix * glm::vec4(vert1.position, 1.0f));
                    tri.v2 = glm::vec3(me.worldMatrix * glm::vec4(vert2.position, 1.0f));

                    tri.n0 = glm::normalize(me.normalMatrix * vert0.normal);
                    tri.n1 = glm::normalize(me.normalMatrix * vert1.normal);
                    tri.n2 = glm::normalize(me.normalMatrix * vert2.normal);

                    tri.uv0 = vert0.texCoords;
                    tri.uv1 = vert1.texCoords;
                    tri.uv2 = vert2.texCoords;

                    tri.entityId = me.entityId;
                    tri.submeshIdx = submeshIdx;

                    if (tri.computeArea() < 1e-8f)
                        continue;

                    triangles.push_back(tri);
                }
            }

            if (progressCallback)
            {
                progressCallback(static_cast<float>(entityIdx + 1) / static_cast<float>(totalEntities));
            }
        }
    }

    void BakeSceneMesh::addTerrainTriangles(const TerrainBakeGeometry& terrain,
                                            std::vector<math::RayBVHTriangle>& triangles)
    {
        if (terrain.vertices.empty() || terrain.triangles.empty())
        {
            return;
        }

        size_t vertCount = terrain.vertices.size() / 3;
        size_t triCount = terrain.triangles.size() / 3;
        vfLogInfo("[LightBake] Adding {} terrain triangles ({} vertices, {} tiles)",
                     triCount, vertCount, terrain.tileInfos.size());

        auto findTileForTriangle = [&](size_t triIdx) -> const TerrainTileBakeInfo*
        {
            for (const auto& tile : terrain.tileInfos)
            {
                if (static_cast<int>(triIdx) >= tile.firstTriangleIndex &&
                    static_cast<int>(triIdx) < tile.firstTriangleIndex + tile.triangleCount)
                {
                    return &tile;
                }
            }
            return nullptr;
        };

        auto computeTileUV = [](const glm::vec3& worldPos, const TerrainTileBakeInfo& tile) -> glm::vec2
        {
            float u = (worldPos.x - tile.worldOrigin.x) / tile.tileSize;
            float v = (worldPos.z - tile.worldOrigin.z) / tile.tileSize;
            return glm::vec2(glm::clamp(u, 0.0f, 1.0f), glm::clamp(v, 0.0f, 1.0f));
        };

        for (size_t i = 0; i + 2 < terrain.triangles.size(); i += 3)
        {
            int i0 = terrain.triangles[i];
            int i1 = terrain.triangles[i + 1];
            int i2 = terrain.triangles[i + 2];

            if (i0 < 0 || i1 < 0 || i2 < 0 ||
                static_cast<size_t>(i0) >= vertCount ||
                static_cast<size_t>(i1) >= vertCount ||
                static_cast<size_t>(i2) >= vertCount)
            {
                continue;
            }

            math::RayBVHTriangle tri;
            tri.v0 = glm::vec3(terrain.vertices[i0 * 3], terrain.vertices[i0 * 3 + 1], terrain.vertices[i0 * 3 + 2]);
            tri.v1 = glm::vec3(terrain.vertices[i1 * 3], terrain.vertices[i1 * 3 + 1], terrain.vertices[i1 * 3 + 2]);
            tri.v2 = glm::vec3(terrain.vertices[i2 * 3], terrain.vertices[i2 * 3 + 1], terrain.vertices[i2 * 3 + 2]);

            glm::vec3 faceNormal = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
            tri.n0 = tri.n1 = tri.n2 = faceNormal;

            size_t triIdx = i / 3;
            const auto* tileInfo = findTileForTriangle(triIdx);
            if (tileInfo)
            {
                tri.entityId = TERRAIN_ENTITY_BASE + static_cast<uint32_t>(
                    &(*tileInfo) - terrain.tileInfos.data());
                tri.submeshIdx = 0;

                tri.uv0 = computeTileUV(tri.v0, *tileInfo);
                tri.uv1 = computeTileUV(tri.v1, *tileInfo);
                tri.uv2 = computeTileUV(tri.v2, *tileInfo);
            }
            else
            {
                // Fallback: shadow-only geometry (no lightmap)
                tri.uv0 = tri.uv1 = tri.uv2 = glm::vec2(0.0f);
                tri.entityId = 0;
                tri.submeshIdx = 0;
            }

            if (tri.computeArea() < 1e-8f)
            {
                continue;
            }

            triangles.push_back(tri);
        }
    }

    void BakeSceneMesh::addWaterTriangles(const std::vector<WaterBakeTile>& waterTiles,
                                          std::vector<math::RayBVHTriangle>& triangles)
    {
        if (waterTiles.empty())
        {
            return;
        }

        size_t totalWaterTris = 0;

        for (const auto& tile : waterTiles)
        {
            uint32_t N = tile.subdivisions;
            float spacing = tile.worldTileSize / static_cast<float>(N);
            glm::vec3 normal(0.0f, 1.0f, 0.0f); // Water always faces up

            for (uint32_t z = 0; z < N; ++z)
            {
                for (uint32_t x = 0; x < N; ++x)
                {
                    glm::vec3 p00(tile.worldOrigin.x + x * spacing, tile.waterHeight, tile.worldOrigin.z + z * spacing);
                    glm::vec3 p10(tile.worldOrigin.x + (x + 1) * spacing, tile.waterHeight,
                                  tile.worldOrigin.z + z * spacing);
                    glm::vec3 p01(tile.worldOrigin.x + x * spacing, tile.waterHeight,
                                  tile.worldOrigin.z + (z + 1) * spacing);
                    glm::vec3 p11(tile.worldOrigin.x + (x + 1) * spacing, tile.waterHeight,
                                  tile.worldOrigin.z + (z + 1) * spacing);

                    // Triangle 1: topLeft, bottomLeft, topRight
                    math::RayBVHTriangle tri1;
                    tri1.v0 = p00;
                    tri1.v1 = p01;
                    tri1.v2 = p10;
                    tri1.n0 = tri1.n1 = tri1.n2 = normal;
                    tri1.uv0 = tri1.uv1 = tri1.uv2 = glm::vec2(0.0f);
                    tri1.entityId = 0;
                    tri1.submeshIdx = 0;
                    triangles.push_back(tri1);

                    // Triangle 2: topRight, bottomLeft, bottomRight
                    math::RayBVHTriangle tri2;
                    tri2.v0 = p10;
                    tri2.v1 = p01;
                    tri2.v2 = p11;
                    tri2.n0 = tri2.n1 = tri2.n2 = normal;
                    tri2.uv0 = tri2.uv1 = tri2.uv2 = glm::vec2(0.0f);
                    tri2.entityId = 0;
                    tri2.submeshIdx = 0;
                    triangles.push_back(tri2);

                    totalWaterTris += 2;
                }
            }
        }

        vfLogInfo("[LightBake] Added {} water triangles from {} tiles", totalWaterTris, waterTiles.size());
    }

    std::optional<math::RayHitResult> BakeSceneMesh::traceRay(
        const math::Ray& ray, float maxDist) const
    {
        return bvh_.queryRay(ray, maxDist);
    }

    bool BakeSceneMesh::traceOcclusion(
        const math::Ray& ray, float maxDist) const
    {
        return bvh_.queryOcclusion(ray, maxDist);
    }

    void BakeSceneMesh::clear()
    {
        bvh_.clear();
    }
}
