#include "NavmeshServiceImpl.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include <glm/gtc/constants.hpp>

namespace services
{
    void NavmeshServiceImpl::collectSceneGeometry(const types::NavmeshBakeSettings& settings,
                                                    navigation::NavmeshInputGeometry& outGeometry)
    {
        if (settings.includeTerrain)
        {
            collectTerrainGeometry(outGeometry);
        }

        if (settings.includeStaticMeshes)
        {
            collectStaticMeshGeometry(outGeometry);
        }

        if (settings.includeColliders)
        {
            collectColliderGeometry(outGeometry);
        }

        collectObstacleGeometry(outGeometry);
    }

    void NavmeshServiceImpl::collectTerrainGeometry(navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto terrainGeometry = dispatcher.query(events::terrain::GetTerrainGeometryQuery{});

        if (terrainGeometry.vertices.empty())
        {
            return;
        }

        int baseVertex = outGeometry.getVertexCount();

        for (size_t i = 0; i + 2 < terrainGeometry.vertices.size(); i += 3)
        {
            outGeometry.addVertex(glm::vec3(
                terrainGeometry.vertices[i],
                terrainGeometry.vertices[i + 1],
                terrainGeometry.vertices[i + 2]));
        }

        for (size_t i = 0; i + 2 < terrainGeometry.triangles.size(); i += 3)
        {
            outGeometry.addTriangle(
                baseVertex + terrainGeometry.triangles[i],
                baseVertex + terrainGeometry.triangles[i + 1],
                baseVertex + terrainGeometry.triangles[i + 2]);
        }

        vfLogInfo("NavmeshService: Collected terrain geometry: {} verts, {} tris",
                  terrainGeometry.vertices.size() / 3, terrainGeometry.triangles.size() / 3);
    }

    void NavmeshServiceImpl::collectStaticMeshGeometry(navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        int totalVerts = 0;
        int totalTris = 0;

        auto view = registry.view<components::MeshComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(entity);
                if (rb.type == components::RigidBodyType::Dynamic)
                {
                    continue;
                }
            }

            // A moving nav-mesh agent (e.g. a unit/soldier) must never be baked into the
            // nav-mesh it walks on — that would carve solid geometry under it and trap it.
            if (registry.all_of<components::NavmeshAgentComponent>(entity))
            {
                continue;
            }

            const auto& mesh = view.get<components::MeshComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            if (!mesh.meshRef.isValid())
            {
                continue;
            }

            auto meshFuture = resource::ResourceManager::loadMeshAsync(mesh.meshRef);
            auto meshesData = meshFuture.get();

            if (!meshesData || meshesData->meshes.empty())
            {
                continue;
            }

            glm::mat4 modelMatrix = transform.getMatrix();

            for (const auto& submesh : meshesData->meshes)
            {
                if (submesh.lodLevels.empty())
                {
                    continue;
                }

                const auto& lod = submesh.lodLevels[0];
                int baseVertex = outGeometry.getVertexCount();

                for (const auto& vertex : lod.vertices)
                {
                    glm::vec3 worldPos = glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                    outGeometry.addVertex(worldPos);
                }

                for (size_t i = 0; i + 2 < lod.indices.size(); i += 3)
                {
                    outGeometry.addTriangle(
                        baseVertex + static_cast<int>(lod.indices[i]),
                        baseVertex + static_cast<int>(lod.indices[i + 1]),
                        baseVertex + static_cast<int>(lod.indices[i + 2]));
                }

                totalVerts += static_cast<int>(lod.vertices.size());
                totalTris += static_cast<int>(lod.indices.size() / 3);
            }
        }

        if (totalVerts > 0)
        {
            vfLogInfo("NavmeshService: Collected static mesh geometry: {} verts, {} tris",
                      totalVerts, totalTris);
        }
    }

    void NavmeshServiceImpl::collectColliderGeometry(navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        int totalVerts = 0;
        int totalTris = 0;

        auto view = registry.view<components::ColliderComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            if (registry.all_of<components::MeshComponent>(entity))
            {
                continue;
            }

            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(entity);
                if (rb.type == components::RigidBodyType::Dynamic)
                {
                    continue;
                }
            }

            // A moving nav-mesh agent (e.g. a unit/soldier) must never be baked into the
            // nav-mesh it walks on — that would carve solid geometry under it and trap it.
            if (registry.all_of<components::NavmeshAgentComponent>(entity))
            {
                continue;
            }

            const auto& collider = view.get<components::ColliderComponent>(entity);
            if (collider.isTrigger)
            {
                continue;
            }

            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::mat4 modelMatrix = transform.getMatrix();
            modelMatrix = glm::translate(modelMatrix, collider.offset);

            if (collider.shape == components::ColliderShape::Box)
            {
                glm::vec3 half = collider.size * 0.5f;
                int base = outGeometry.getVertexCount();

                glm::vec3 corners[8] = {
                    {-half.x, -half.y, -half.z}, { half.x, -half.y, -half.z},
                    { half.x,  half.y, -half.z}, {-half.x,  half.y, -half.z},
                    {-half.x, -half.y,  half.z}, { half.x, -half.y,  half.z},
                    { half.x,  half.y,  half.z}, {-half.x,  half.y,  half.z}
                };

                for (const auto& corner : corners)
                {
                    outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(corner, 1.0f)));
                }

                int boxIndices[] = {
                    0,1,2, 0,2,3,
                    5,4,7, 5,7,6,
                    4,0,3, 4,3,7,
                    1,5,6, 1,6,2,
                    3,2,6, 3,6,7,
                    4,5,1, 4,1,0
                };
                for (int i = 0; i < 36; i += 3)
                {
                    outGeometry.addTriangle(base + boxIndices[i], base + boxIndices[i+1], base + boxIndices[i+2]);
                }

                totalVerts += 8;
                totalTris += 12;
            }
            else if (collider.shape == components::ColliderShape::ConvexMesh ||
                     collider.shape == components::ColliderShape::TriangleMesh)
            {
                if (!collider.meshRef.isValid())
                {
                    continue;
                }

                auto meshFuture = resource::ResourceManager::loadMeshAsync(collider.meshRef);
                auto meshesData = meshFuture.get();
                if (!meshesData || meshesData->meshes.empty())
                {
                    continue;
                }

                for (const auto& submesh : meshesData->meshes)
                {
                    if (submesh.lodLevels.empty()) continue;
                    const auto& lod = submesh.lodLevels[0];
                    int base = outGeometry.getVertexCount();

                    for (const auto& vertex : lod.vertices)
                    {
                        outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f)));
                    }

                    for (size_t i = 0; i + 2 < lod.indices.size(); i += 3)
                    {
                        outGeometry.addTriangle(
                            base + static_cast<int>(lod.indices[i]),
                            base + static_cast<int>(lod.indices[i + 1]),
                            base + static_cast<int>(lod.indices[i + 2]));
                    }

                    totalVerts += static_cast<int>(lod.vertices.size());
                    totalTris += static_cast<int>(lod.indices.size() / 3);
                }
            }
        }

        if (totalVerts > 0)
        {
            vfLogInfo("NavmeshService: Collected collider geometry: {} verts, {} tris",
                      totalVerts, totalTris);
        }
    }

    // === Per-Tile Geometry Collection (VK-775) ===

    static bool boundsOverlap2D(const navigation::NavmeshTileBounds& a,
                                 const glm::vec3& bmin, const glm::vec3& bmax)
    {
        return a.max.x >= bmin.x && a.min.x <= bmax.x &&
               a.max.z >= bmin.z && a.min.z <= bmax.z;
    }

    void NavmeshServiceImpl::collectTileGeometry(const navigation::NavmeshTileBounds& bounds,
                                                   const types::NavmeshBakeSettings& settings,
                                                   navigation::NavmeshInputGeometry& outGeometry)
    {
        auto expanded = navigation::expandBoundsForOverlap(bounds, settings);

        if (settings.includeTerrain)
        {
            collectTerrainGeometryForBounds(expanded, outGeometry);
        }
        if (settings.includeStaticMeshes)
        {
            collectStaticMeshGeometryForBounds(expanded, outGeometry);
        }
        if (settings.includeColliders)
        {
            collectColliderGeometryForBounds(expanded, outGeometry);
        }

        collectObstacleGeometryForBounds(expanded, outGeometry);
    }

    void NavmeshServiceImpl::collectTerrainGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                                               navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto bakeGeometry = dispatcher.query(events::terrain::GetTerrainBakeGeometryQuery{});

        if (bakeGeometry.vertices.empty())
            return;

        for (const auto& tileInfo : bakeGeometry.tileInfos)
        {
            glm::vec3 tileMin = tileInfo.worldOrigin;
            glm::vec3 tileMax = tileMin + glm::vec3(tileInfo.tileSize, 1e6f, tileInfo.tileSize);

            if (!boundsOverlap2D(bounds, tileMin, tileMax))
                continue;

            int baseVertex = outGeometry.getVertexCount();

            int vStart = tileInfo.firstVertexIndex * 3;
            int vEnd = vStart + tileInfo.vertexCount * 3;
            for (int i = vStart; i < vEnd; i += 3)
            {
                outGeometry.addVertex(glm::vec3(
                    bakeGeometry.vertices[i],
                    bakeGeometry.vertices[i + 1],
                    bakeGeometry.vertices[i + 2]));
            }

            int tStart = tileInfo.firstTriangleIndex * 3;
            int tEnd = tStart + tileInfo.triangleCount * 3;
            for (int i = tStart; i < tEnd; i += 3)
            {
                outGeometry.addTriangle(
                    baseVertex + bakeGeometry.triangles[i] - tileInfo.firstVertexIndex,
                    baseVertex + bakeGeometry.triangles[i + 1] - tileInfo.firstVertexIndex,
                    baseVertex + bakeGeometry.triangles[i + 2] - tileInfo.firstVertexIndex);
            }
        }
    }

    void NavmeshServiceImpl::collectStaticMeshGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                                                  navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::MeshComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(entity);
                if (rb.type == components::RigidBodyType::Dynamic)
                    continue;
            }

            // A moving nav-mesh agent (e.g. a unit/soldier) must never be baked into the
            // nav-mesh it walks on — that would carve solid geometry under it and trap it.
            if (registry.all_of<components::NavmeshAgentComponent>(entity))
                continue;

            const auto& transform = view.get<components::TransformComponent>(entity);
            glm::vec3 pos = transform.position;

            // Quick position-based overlap check
            if (pos.x < bounds.min.x - 50.0f || pos.x > bounds.max.x + 50.0f ||
                pos.z < bounds.min.z - 50.0f || pos.z > bounds.max.z + 50.0f)
                continue;

            const auto& mesh = view.get<components::MeshComponent>(entity);
            if (!mesh.meshRef.isValid())
                continue;

            auto meshFuture = resource::ResourceManager::loadMeshAsync(mesh.meshRef);
            auto meshesData = meshFuture.get();
            if (!meshesData || meshesData->meshes.empty())
                continue;

            glm::mat4 modelMatrix = transform.getMatrix();

            for (const auto& submesh : meshesData->meshes)
            {
                if (submesh.lodLevels.empty())
                    continue;

                const auto& lod = submesh.lodLevels[0];
                int baseVertex = outGeometry.getVertexCount();

                for (const auto& vertex : lod.vertices)
                {
                    glm::vec3 worldPos = glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                    outGeometry.addVertex(worldPos);
                }

                for (size_t i = 0; i + 2 < lod.indices.size(); i += 3)
                {
                    outGeometry.addTriangle(
                        baseVertex + static_cast<int>(lod.indices[i]),
                        baseVertex + static_cast<int>(lod.indices[i + 1]),
                        baseVertex + static_cast<int>(lod.indices[i + 2]));
                }
            }
        }
    }

    void NavmeshServiceImpl::collectColliderGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                                                navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::ColliderComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            if (registry.all_of<components::MeshComponent>(entity))
                continue;

            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                const auto& rb = registry.get<components::RigidBodyComponent>(entity);
                if (rb.type == components::RigidBodyType::Dynamic)
                    continue;
            }

            // A moving nav-mesh agent (e.g. a unit/soldier) must never be baked into the
            // nav-mesh it walks on — that would carve solid geometry under it and trap it.
            if (registry.all_of<components::NavmeshAgentComponent>(entity))
                continue;

            const auto& collider = view.get<components::ColliderComponent>(entity);
            if (collider.isTrigger)
                continue;

            const auto& transform = view.get<components::TransformComponent>(entity);
            glm::vec3 pos = transform.position;

            if (pos.x < bounds.min.x - 50.0f || pos.x > bounds.max.x + 50.0f ||
                pos.z < bounds.min.z - 50.0f || pos.z > bounds.max.z + 50.0f)
                continue;

            glm::mat4 modelMatrix = transform.getMatrix();
            modelMatrix = glm::translate(modelMatrix, collider.offset);

            if (collider.shape == components::ColliderShape::Box)
            {
                glm::vec3 half = collider.size * 0.5f;
                int base = outGeometry.getVertexCount();

                glm::vec3 corners[8] = {
                    {-half.x, -half.y, -half.z}, { half.x, -half.y, -half.z},
                    { half.x,  half.y, -half.z}, {-half.x,  half.y, -half.z},
                    {-half.x, -half.y,  half.z}, { half.x, -half.y,  half.z},
                    { half.x,  half.y,  half.z}, {-half.x,  half.y,  half.z}
                };

                for (const auto& corner : corners)
                {
                    outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(corner, 1.0f)));
                }

                int boxIndices[] = {
                    0,1,2, 0,2,3, 5,4,7, 5,7,6,
                    4,0,3, 4,3,7, 1,5,6, 1,6,2,
                    3,2,6, 3,6,7, 4,5,1, 4,1,0
                };
                for (int i = 0; i < 36; i += 3)
                {
                    outGeometry.addTriangle(base + boxIndices[i], base + boxIndices[i+1], base + boxIndices[i+2]);
                }
            }
            else if (collider.shape == components::ColliderShape::ConvexMesh ||
                     collider.shape == components::ColliderShape::TriangleMesh)
            {
                if (!collider.meshRef.isValid())
                    continue;

                auto meshFuture = resource::ResourceManager::loadMeshAsync(collider.meshRef);
                auto meshesData = meshFuture.get();
                if (!meshesData || meshesData->meshes.empty())
                    continue;

                for (const auto& submesh : meshesData->meshes)
                {
                    if (submesh.lodLevels.empty()) continue;
                    const auto& lod = submesh.lodLevels[0];
                    int base = outGeometry.getVertexCount();

                    for (const auto& vertex : lod.vertices)
                    {
                        outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f)));
                    }

                    for (size_t i = 0; i + 2 < lod.indices.size(); i += 3)
                    {
                        outGeometry.addTriangle(
                            base + static_cast<int>(lod.indices[i]),
                            base + static_cast<int>(lod.indices[i + 1]),
                            base + static_cast<int>(lod.indices[i + 2]));
                    }
                }
            }
        }
    }

    // === Off-Mesh Link Collection ===

    static uint8_t linkTypeToAreaType(components::OffMeshLinkType type)
    {
        switch (type)
        {
            case components::OffMeshLinkType::Jump:  return types::NAVMESH_AREA_JUMP;
            case components::OffMeshLinkType::Climb: return types::NAVMESH_AREA_CLIMB;
            case components::OffMeshLinkType::Drop:  return types::NAVMESH_AREA_DROP;
            case components::OffMeshLinkType::Custom: return types::NAVMESH_AREA_CUSTOM;
            default: return types::NAVMESH_AREA_GROUND;
        }
    }

    navigation::OffMeshConnectionsMap NavmeshServiceImpl::collectAllOffMeshLinks(
        const types::NavmeshBakeSettings& settings)
    {
        navigation::OffMeshConnectionsMap result;
        auto& registry = scene::EntityRegistry::getRegistry();

        float tileWorldSize = settings.tileSize * settings.cellSize;
        if (tileWorldSize <= 0.0f)
            return result;

        auto view = registry.view<components::OffMeshLinkComponent, components::TransformComponent>();
        uint32_t nextUserID = 1;
        for (auto entity : view)
        {
            const auto& link = view.get<components::OffMeshLinkComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldStart = transform.position + link.startOffset;
            glm::vec3 worldEnd = transform.position + link.endOffset;

            navigation::NavmeshTileCoord tileCoord;
            tileCoord.x = static_cast<int32_t>(floorf(worldStart.x / tileWorldSize));
            tileCoord.z = static_cast<int32_t>(floorf(worldStart.z / tileWorldSize));

            navigation::NavmeshOffMeshConnection conn;
            conn.start = worldStart;
            conn.end = worldEnd;
            conn.radius = link.radius;
            conn.direction = static_cast<uint8_t>(link.direction);
            conn.areaType = link.areaType > 0 ? link.areaType : linkTypeToAreaType(link.linkType);
            conn.flags = link.polyFlags;
            conn.userID = nextUserID++;

            result[tileCoord].connections.push_back(conn);
        }

        return result;
    }

    navigation::NavmeshOffMeshConnections NavmeshServiceImpl::collectOffMeshLinksForTile(
        const navigation::NavmeshTileBounds& bounds,
        const types::NavmeshBakeSettings& settings)
    {
        navigation::NavmeshOffMeshConnections result;
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::OffMeshLinkComponent, components::TransformComponent>();
        uint32_t nextUserID = 1;
        for (auto entity : view)
        {
            const auto& link = view.get<components::OffMeshLinkComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldStart = transform.position + link.startOffset;

            if (worldStart.x < bounds.min.x || worldStart.x > bounds.max.x ||
                worldStart.z < bounds.min.z || worldStart.z > bounds.max.z)
                continue;

            glm::vec3 worldEnd = transform.position + link.endOffset;

            navigation::NavmeshOffMeshConnection conn;
            conn.start = worldStart;
            conn.end = worldEnd;
            conn.radius = link.radius;
            conn.direction = static_cast<uint8_t>(link.direction);
            conn.areaType = link.areaType > 0 ? link.areaType : linkTypeToAreaType(link.linkType);
            conn.flags = link.polyFlags;
            conn.userID = nextUserID++;

            result.connections.push_back(conn);
        }

        return result;
    }

    // === Auto-Generation of Drop Links ===

    void NavmeshServiceImpl::autoGenerateDropLinks(
        const types::NavmeshBakeSettings& settings,
        navigation::OffMeshConnectionsMap& outMap)
    {
        if (!navmeshProvider->hasNavmesh())
            return;

        // Get the built navmesh geometry to find boundary edges
        std::vector<glm::vec3> meshVerts;
        std::vector<uint32_t> meshIndices;
        navmeshProvider->getDebugMesh(meshVerts, meshIndices);

        if (meshVerts.empty() || meshIndices.empty())
            return;

        float tileWorldSize = settings.tileSize * settings.cellSize;

        // Build edge map to find boundary edges (edges used by only one triangle)
        struct Edge
        {
            uint32_t v0, v1;
            bool operator==(const Edge& o) const { return v0 == o.v0 && v1 == o.v1; }
        };
        struct EdgeHash
        {
            size_t operator()(const Edge& e) const
            {
                return std::hash<uint64_t>{}((uint64_t(e.v0) << 32) | e.v1);
            }
        };

        std::unordered_map<Edge, int, EdgeHash> edgeCount;
        auto makeEdge = [](uint32_t a, uint32_t b) -> Edge
        {
            return a < b ? Edge{a, b} : Edge{b, a};
        };

        for (size_t i = 0; i + 2 < meshIndices.size(); i += 3)
        {
            edgeCount[makeEdge(meshIndices[i], meshIndices[i + 1])]++;
            edgeCount[makeEdge(meshIndices[i + 1], meshIndices[i + 2])]++;
            edgeCount[makeEdge(meshIndices[i + 2], meshIndices[i])]++;
        }

        uint32_t autoID = 100000;
        int generated = 0;

        for (const auto& [edge, count] : edgeCount)
        {
            if (count != 1)
                continue; // Only boundary edges

            const glm::vec3& p0 = meshVerts[edge.v0];
            const glm::vec3& p1 = meshVerts[edge.v1];
            glm::vec3 edgeMid = (p0 + p1) * 0.5f;

            // Try both perpendicular directions to find the outward one
            glm::vec3 edgeDir = glm::normalize(p1 - p0);
            glm::vec3 perp1 = glm::vec3(-edgeDir.z, 0.0f, edgeDir.x);
            glm::vec3 perp2 = -perp1;

            // The outward direction is the one NOT on the navmesh
            glm::vec3 test1 = edgeMid + perp1 * settings.agentRadius * 2.0f;
            glm::vec3 test2 = edgeMid + perp2 * settings.agentRadius * 2.0f;
            bool onMesh1 = navmeshProvider->isPointOnNavmesh(test1, 0.5f);
            bool onMesh2 = navmeshProvider->isPointOnNavmesh(test2, 0.5f);

            glm::vec3 outwardNormal;
            if (!onMesh1 && onMesh2)
                outwardNormal = perp1;
            else if (onMesh1 && !onMesh2)
                outwardNormal = perp2;
            else
                continue; // Ambiguous or both off-mesh, skip

            // Step outward and check for ground below
            glm::vec3 testPoint = edgeMid + outwardNormal * settings.agentRadius * 2.0f;

            glm::vec3 dropTest = testPoint - glm::vec3(0.0f, settings.autoDropMaxHeight, 0.0f);
            glm::vec3 closest = navmeshProvider->getClosestPoint(dropTest, settings.autoDropMaxHeight);

            float heightDiff = edgeMid.y - closest.y;

            if (heightDiff >= settings.autoDropMinHeight && heightDiff <= settings.autoDropMaxHeight)
            {
                // Verify the landing point is actually on the navmesh
                if (!navmeshProvider->isPointOnNavmesh(closest, 0.5f))
                    continue;

                navigation::NavmeshOffMeshConnection conn;
                conn.start = edgeMid;
                conn.end = closest;
                conn.radius = settings.agentRadius;
                conn.direction = 0; // One-way (drop down only)
                conn.areaType = types::NAVMESH_AREA_DROP;
                conn.flags = 1;
                conn.userID = autoID++;

                navigation::NavmeshTileCoord tileCoord;
                tileCoord.x = static_cast<int32_t>(floorf(conn.start.x / tileWorldSize));
                tileCoord.z = static_cast<int32_t>(floorf(conn.start.z / tileWorldSize));

                outMap[tileCoord].connections.push_back(conn);
                generated++;
            }
        }

        if (generated > 0)
            vfLogInfo("NavmeshService: Auto-generated {} drop links", generated);
    }

    // === Obstacle Geometry Collection ===

    static void generateBoxGeometry(const glm::mat4& modelMatrix, const glm::vec3& halfSize,
                                     navigation::NavmeshInputGeometry& outGeometry)
    {
        int base = outGeometry.getVertexCount();
        glm::vec3 corners[8] = {
            {-halfSize.x, -halfSize.y, -halfSize.z}, { halfSize.x, -halfSize.y, -halfSize.z},
            { halfSize.x,  halfSize.y, -halfSize.z}, {-halfSize.x,  halfSize.y, -halfSize.z},
            {-halfSize.x, -halfSize.y,  halfSize.z}, { halfSize.x, -halfSize.y,  halfSize.z},
            { halfSize.x,  halfSize.y,  halfSize.z}, {-halfSize.x,  halfSize.y,  halfSize.z}
        };
        for (const auto& corner : corners)
            outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(corner, 1.0f)));

        int boxIndices[] = {
            0,1,2, 0,2,3, 5,4,7, 5,7,6,
            4,0,3, 4,3,7, 1,5,6, 1,6,2,
            3,2,6, 3,6,7, 4,5,1, 4,1,0
        };
        for (int i = 0; i < 36; i += 3)
            outGeometry.addTriangle(base + boxIndices[i], base + boxIndices[i+1], base + boxIndices[i+2]);
    }

    static void generateCylinderGeometry(const glm::mat4& modelMatrix, float radius, float height,
                                          navigation::NavmeshInputGeometry& outGeometry)
    {
        constexpr int SEGMENTS = 16;
        float halfH = height * 0.5f;
        int baseVertex = outGeometry.getVertexCount();

        // Generate ring vertices (bottom then top)
        for (int ring = 0; ring < 2; ++ring)
        {
            float y = (ring == 0) ? -halfH : halfH;
            for (int i = 0; i < SEGMENTS; ++i)
            {
                float angle = (glm::two_pi<float>() * i) / SEGMENTS;
                glm::vec3 local(radius * cosf(angle), y, radius * sinf(angle));
                outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(local, 1.0f)));
            }
        }

        // Center vertices for caps
        int bottomCenter = outGeometry.getVertexCount();
        outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(0.0f, -halfH, 0.0f, 1.0f)));
        int topCenter = outGeometry.getVertexCount();
        outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(0.0f, halfH, 0.0f, 1.0f)));

        for (int i = 0; i < SEGMENTS; ++i)
        {
            int next = (i + 1) % SEGMENTS;

            // Side quads (2 triangles each)
            int b0 = baseVertex + i;
            int b1 = baseVertex + next;
            int t0 = baseVertex + SEGMENTS + i;
            int t1 = baseVertex + SEGMENTS + next;
            outGeometry.addTriangle(b0, b1, t1);
            outGeometry.addTriangle(b0, t1, t0);

            // Bottom cap
            outGeometry.addTriangle(bottomCenter, baseVertex + next, baseVertex + i);
            // Top cap
            outGeometry.addTriangle(topCenter, baseVertex + SEGMENTS + i, baseVertex + SEGMENTS + next);
        }
    }

    void NavmeshServiceImpl::collectObstacleGeometry(navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshObstacleComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
            if (obstacle.mode != components::NavmeshObstacleMode::Carve)
                continue;

            const auto& transform = view.get<components::TransformComponent>(entity);
            glm::mat4 modelMatrix = glm::translate(transform.getMatrix(), obstacle.offset);

            if (obstacle.shape == components::NavmeshObstacleShape::Box)
            {
                generateBoxGeometry(modelMatrix, obstacle.size * 0.5f, outGeometry);
            }
            else
            {
                generateCylinderGeometry(modelMatrix, obstacle.size.x, obstacle.size.y, outGeometry);
            }
        }
    }

    void NavmeshServiceImpl::collectObstacleGeometryForBounds(const navigation::NavmeshTileBounds& bounds,
                                                                navigation::NavmeshInputGeometry& outGeometry)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshObstacleComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
            if (obstacle.mode != components::NavmeshObstacleMode::Carve)
                continue;

            const auto& transform = view.get<components::TransformComponent>(entity);
            glm::vec3 pos = transform.position + obstacle.offset;

            float maxExtent = glm::max(obstacle.size.x, glm::max(obstacle.size.y, obstacle.size.z)) * 0.5f;
            if (pos.x < bounds.min.x - maxExtent || pos.x > bounds.max.x + maxExtent ||
                pos.z < bounds.min.z - maxExtent || pos.z > bounds.max.z + maxExtent)
                continue;

            glm::mat4 modelMatrix = glm::translate(transform.getMatrix(), obstacle.offset);

            if (obstacle.shape == components::NavmeshObstacleShape::Box)
            {
                generateBoxGeometry(modelMatrix, obstacle.size * 0.5f, outGeometry);
            }
            else
            {
                generateCylinderGeometry(modelMatrix, obstacle.size.x, obstacle.size.y, outGeometry);
            }
        }
    }

    // === Area Modifier Volume Collection ===

    navigation::AreaModifiersMap NavmeshServiceImpl::collectAllAreaModifiers()
    {
        navigation::AreaModifiersMap result;
        auto& registry = scene::EntityRegistry::getRegistry();

        float tileWorldSize = lastBakeSettings.tileSize * lastBakeSettings.cellSize;
        if (tileWorldSize <= 0.0f)
            return result;

        auto view = registry.view<components::NavmeshModifierVolumeComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& volume = view.get<components::NavmeshModifierVolumeComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldPos = transform.position + volume.offset;
            glm::vec3 half = (volume.shape == components::NavmeshModifierVolumeShape::Box)
                ? volume.size * 0.5f
                : glm::vec3(volume.size.x, volume.size.y * 0.5f, volume.size.x);

            navigation::NavmeshAreaModifier mod;
            mod.shape = static_cast<uint8_t>(volume.shape);
            mod.position = worldPos;
            mod.halfSize = half;
            mod.areaType = volume.areaType;

            // Assign to all tiles the volume overlaps
            auto coordMin = navigation::NavmeshTileCoord{
                static_cast<int32_t>(floorf((worldPos.x - half.x) / tileWorldSize)),
                static_cast<int32_t>(floorf((worldPos.z - half.z) / tileWorldSize))
            };
            auto coordMax = navigation::NavmeshTileCoord{
                static_cast<int32_t>(floorf((worldPos.x + half.x) / tileWorldSize)),
                static_cast<int32_t>(floorf((worldPos.z + half.z) / tileWorldSize))
            };

            for (int tx = coordMin.x; tx <= coordMax.x; ++tx)
                for (int tz = coordMin.z; tz <= coordMax.z; ++tz)
                    result[{tx, tz}].push_back(mod);
        }

        return result;
    }

    std::vector<navigation::NavmeshAreaModifier> NavmeshServiceImpl::collectAreaModifiersForTile(
        const navigation::NavmeshTileBounds& bounds)
    {
        std::vector<navigation::NavmeshAreaModifier> result;
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::NavmeshModifierVolumeComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& volume = view.get<components::NavmeshModifierVolumeComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldPos = transform.position + volume.offset;
            glm::vec3 half = (volume.shape == components::NavmeshModifierVolumeShape::Box)
                ? volume.size * 0.5f
                : glm::vec3(volume.size.x, volume.size.y * 0.5f, volume.size.x);

            // Check if modifier overlaps tile bounds
            glm::vec3 modMin = worldPos - half;
            glm::vec3 modMax = worldPos + half;

            if (modMax.x < bounds.min.x || modMin.x > bounds.max.x ||
                modMax.z < bounds.min.z || modMin.z > bounds.max.z)
                continue;

            navigation::NavmeshAreaModifier mod;
            mod.shape = static_cast<uint8_t>(volume.shape);
            mod.position = worldPos;
            mod.halfSize = half;
            mod.areaType = volume.areaType;

            result.push_back(mod);
        }

        return result;
    }
}
