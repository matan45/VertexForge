#include "SceneGeometryCollector.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/foliage/FoliageEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "foliage/FoliageNavGeometry.hpp"
#include "print/Log.hpp"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    namespace
    {
        // Meshes and colliders are matched to bounds by their world origin, with this margin for their extent.
        constexpr float ENTITY_BOUNDS_MARGIN = 50.0f;

        bool overlaps2D(const navigation::NavmeshTileBounds& a, const glm::vec3& bmin, const glm::vec3& bmax)
        {
            return a.max.x >= bmin.x && a.min.x <= bmax.x &&
                   a.max.z >= bmin.z && a.min.z <= bmax.z;
        }

        bool originNear2D(const navigation::NavmeshTileBounds& bounds, const glm::vec3& origin, float margin)
        {
            return origin.x >= bounds.min.x - margin && origin.x <= bounds.max.x + margin &&
                   origin.z >= bounds.min.z - margin && origin.z <= bounds.max.z + margin;
        }

        // A moving entity is never baked into the navigation data it moves through - that would put solid
        // geometry under it and trap it.
        bool isMover(const entt::registry& registry, entt::entity entity)
        {
            if (registry.any_of<components::NavmeshAgentComponent, components::VolumetricAgentComponent,
                                components::ControllerComponent>(entity))
            {
                return true;
            }
            const auto* body = registry.try_get<components::RigidBodyComponent>(entity);
            return body && body->type == components::RigidBodyType::Dynamic;
        }

        // LOD 0 of every submesh of `meshRef`, transformed by `modelMatrix`.
        void appendMeshLod0(const asset::AssetRef& meshRef, const glm::mat4& modelMatrix,
                            navigation::NavmeshInputGeometry& outGeometry, int& totalVerts, int& totalTris)
        {
            auto meshFuture = resource::ResourceManager::loadMeshAsync(meshRef);
            auto meshesData = meshFuture.get();
            if (!meshesData || meshesData->meshes.empty())
                return;

            for (const auto& submesh : meshesData->meshes)
            {
                if (submesh.lodLevels.empty())
                    continue;

                const auto& lod = submesh.lodLevels[0];
                int baseVertex = outGeometry.getVertexCount();

                for (const auto& vertex : lod.vertices)
                {
                    outGeometry.addVertex(glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f)));
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

        void generateBoxGeometry(const glm::mat4& modelMatrix, const glm::vec3& halfSize,
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

        void generateCylinderGeometry(const glm::mat4& modelMatrix, float radius, float height,
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

        void collectTerrain(const navigation::NavmeshTileBounds* bounds, navigation::NavmeshInputGeometry& outGeometry)
        {
            auto& dispatcher = ::events::EventDispatcher::instance();

            if (!bounds)
            {
                auto terrainGeometry = dispatcher.query(events::terrain::GetTerrainGeometryQuery{});
                if (terrainGeometry.vertices.empty())
                    return;

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

                vfLogInfo("SceneGeometryCollector: Collected terrain geometry: {} verts, {} tris",
                          terrainGeometry.vertices.size() / 3, terrainGeometry.triangles.size() / 3);
                return;
            }

            auto bakeGeometry = dispatcher.query(events::terrain::GetTerrainBakeGeometryQuery{});
            if (bakeGeometry.vertices.empty())
                return;

            for (const auto& tileInfo : bakeGeometry.tileInfos)
            {
                glm::vec3 tileMin = tileInfo.worldOrigin;
                glm::vec3 tileMax = tileMin + glm::vec3(tileInfo.tileSize, 1e6f, tileInfo.tileSize);

                if (!overlaps2D(*bounds, tileMin, tileMax))
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

        void collectStaticMeshes(const navigation::NavmeshTileBounds* bounds,
                                 navigation::NavmeshInputGeometry& outGeometry)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            int totalVerts = 0;
            int totalTris = 0;

            auto view = registry.view<components::MeshComponent, components::TransformComponent>();
            for (auto entity : view)
            {
                if (isMover(registry, entity))
                    continue;

                const glm::mat4 modelMatrix = scene::composeWorldMatrix(registry, entity);
                if (bounds && !originNear2D(*bounds, glm::vec3(modelMatrix[3]), ENTITY_BOUNDS_MARGIN))
                    continue;

                const auto& mesh = view.get<components::MeshComponent>(entity);
                if (!mesh.meshRef.isValid())
                    continue;

                appendMeshLod0(mesh.meshRef, modelMatrix, outGeometry, totalVerts, totalTris);
            }

            if (!bounds && totalVerts > 0)
            {
                vfLogInfo("SceneGeometryCollector: Collected static mesh geometry: {} verts, {} tris",
                          totalVerts, totalTris);
            }
        }

        void collectColliders(const navigation::NavmeshTileBounds* bounds, navigation::NavmeshInputGeometry& outGeometry)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            int totalVerts = 0;
            int totalTris = 0;

            auto view = registry.view<components::ColliderComponent, components::TransformComponent>();
            for (auto entity : view)
            {
                // An entity with a mesh contributes the mesh itself.
                if (registry.all_of<components::MeshComponent>(entity))
                    continue;

                if (isMover(registry, entity))
                    continue;

                const auto& collider = view.get<components::ColliderComponent>(entity);
                if (collider.isTrigger)
                    continue;

                const glm::mat4 entityMatrix = scene::composeWorldMatrix(registry, entity);
                if (bounds && !originNear2D(*bounds, glm::vec3(entityMatrix[3]), ENTITY_BOUNDS_MARGIN))
                    continue;

                const glm::mat4 modelMatrix = glm::translate(entityMatrix, collider.offset);

                if (collider.shape == components::ColliderShape::Box)
                {
                    generateBoxGeometry(modelMatrix, collider.size * 0.5f, outGeometry);
                    totalVerts += 8;
                    totalTris += 12;
                }
                else if (collider.shape == components::ColliderShape::ConvexMesh ||
                         collider.shape == components::ColliderShape::TriangleMesh)
                {
                    if (!collider.meshRef.isValid())
                        continue;

                    appendMeshLod0(collider.meshRef, modelMatrix, outGeometry, totalVerts, totalTris);
                }
            }

            if (!bounds && totalVerts > 0)
            {
                vfLogInfo("SceneGeometryCollector: Collected collider geometry: {} verts, {} tris",
                          totalVerts, totalTris);
            }
        }

        void collectFoliageInBounds(const navigation::NavmeshTileBounds& bounds,
                                    navigation::NavmeshInputGeometry& outGeometry)
        {
            auto& dispatcher = ::events::EventDispatcher::instance();

            // Palette: bail unless at least one type opts into navmesh contribution (grass never does).
            auto palette = dispatcher.query(events::foliage::GetFoliagePaletteQuery{});
            if (palette.empty())
                return;
            bool anyNav = false;
            for (const auto& t : palette)
                if (t.navContribute) { anyNav = true; break; }
            if (!anyNav)
                return;

            // Enumerate terrain tiles overlapping the bounds (reuse the terrain bake tile list, which
            // carries each tile's coord + world AABB - the same enumeration collectTerrain uses).
            auto bakeGeometry = dispatcher.query(events::terrain::GetTerrainBakeGeometryQuery{});
            if (bakeGeometry.tileInfos.empty())
                return;

            // Per meshPath: the coarse (LOD3) geometry loaded ONCE, positions + indices flattened across
            // submeshes. Reused across every instance of that type - never a per-instance mesh load.
            struct TypeMesh
            {
                std::vector<glm::vec3> positions;
                std::vector<uint32_t>  indices;
            };
            std::unordered_map<std::string, TypeMesh> meshCache;

            auto getTypeMesh = [&meshCache](const std::string& meshPath) -> const TypeMesh& {
                auto it = meshCache.find(meshPath);
                if (it != meshCache.end())
                    return it->second;

                TypeMesh tm;
                auto meshRef = asset::AssetRef::fromPath(meshPath);
                if (meshRef.isValid())
                {
                    auto meshesData = resource::ResourceManager::loadMeshAsync(meshRef).get();
                    if (meshesData)
                    {
                        for (const auto& submesh : meshesData->meshes)
                        {
                            if (submesh.lodLevels.empty())
                                continue;
                            const auto& lod = submesh.lodLevels.back(); // coarsest LOD (~12.5% of LOD0)
                            const uint32_t base = static_cast<uint32_t>(tm.positions.size());
                            for (const auto& v : lod.vertices)
                                tm.positions.push_back(v.position);
                            for (uint32_t idx : lod.indices)
                                tm.indices.push_back(base + idx);
                        }
                    }
                }
                return meshCache.emplace(meshPath, std::move(tm)).first->second;
            };

            int totalVerts = 0;
            int totalTris = 0;

            for (const auto& tileInfo : bakeGeometry.tileInfos)
            {
                glm::vec3 tileMin = tileInfo.worldOrigin;
                glm::vec3 tileMax = tileMin + glm::vec3(tileInfo.tileSize, 1e6f, tileInfo.tileSize);
                if (!overlaps2D(bounds, tileMin, tileMax))
                    continue;

                events::foliage::GetTileFoliageInstancesQuery instQuery;
                instQuery.tileX = tileInfo.coordX;
                instQuery.tileZ = tileInfo.coordZ;
                auto instances = dispatcher.query(instQuery);
                if (instances.empty())
                    continue;

                for (const auto& inst : instances)
                {
                    if (inst.typeIndex >= palette.size())
                        continue;
                    const auto& type = palette[inst.typeIndex];
                    if (!type.navContribute || type.meshPath.empty())
                        continue;

                    const TypeMesh& tm = getTypeMesh(type.meshPath);
                    if (tm.positions.empty() || tm.indices.empty())
                        continue;

                    foliage::appendFoliageInstanceGeometry(inst, type, tm.positions, tm.indices, outGeometry);
                    totalVerts += static_cast<int>(tm.positions.size());
                    totalTris += static_cast<int>(tm.indices.size() / 3);
                }
            }

            if (totalVerts > 0)
                vfLogInfo("SceneGeometryCollector: Collected foliage geometry: {} verts, {} tris ({} types)",
                          totalVerts, totalTris, meshCache.size());
        }

        void collectFoliage(const navigation::NavmeshTileBounds* bounds, navigation::NavmeshInputGeometry& outGeometry)
        {
            if (bounds)
            {
                collectFoliageInBounds(*bounds, outGeometry);
                return;
            }

            // code-review #7: foliage contribution is limited to currently-RESIDENT terrain tiles
            // (collectFoliageInBounds enumerates GetTerrainBakeGeometryQuery.tileInfos, which comes from
            // the loaded terrain grid). In a world-sector-streamed scene, foliage on streamed-out tiles
            // is NOT baked, so agents can path straight through those trees/rocks. This matches the
            // terrain-geometry bake (also resident-tile based); warn once on the whole-scene path so the
            // exclusion is not silent.
            {
                auto palette = ::events::EventDispatcher::instance().query(
                    events::foliage::GetFoliagePaletteQuery{});
                bool anyNav = false;
                for (const auto& t : palette)
                    if (t.navContribute) { anyNav = true; break; }
                if (anyNav)
                    vfLogWarning("SceneGeometryCollector: foliage nav contribution only covers currently-resident "
                                 "terrain tiles; foliage on streamed-out tiles is excluded. Load the "
                                 "relevant area before baking to include it.");
            }

            // Whole-scene bake: no bounds filter - collect every nav-contributing foliage instance.
            navigation::NavmeshTileBounds all;
            all.min = glm::vec3(-1e9f);
            all.max = glm::vec3(1e9f);
            collectFoliageInBounds(all, outGeometry);
        }

        void collectObstacles(const navigation::NavmeshTileBounds* bounds, navigation::NavmeshInputGeometry& outGeometry)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto view = registry.view<components::NavmeshObstacleComponent, components::TransformComponent>();

            for (auto entity : view)
            {
                const auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
                if (obstacle.mode != components::NavmeshObstacleMode::Carve)
                    continue;

                const glm::mat4 entityMatrix = scene::composeWorldMatrix(registry, entity);
                if (bounds)
                {
                    const glm::vec3 center = glm::vec3(entityMatrix[3]) + obstacle.offset;
                    const float maxExtent = glm::max(obstacle.size.x, glm::max(obstacle.size.y, obstacle.size.z)) * 0.5f;
                    if (!originNear2D(*bounds, center, maxExtent))
                        continue;
                }

                const glm::mat4 modelMatrix = glm::translate(entityMatrix, obstacle.offset);

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
    }

    SceneGeometryCollector::SceneGeometryCollector(const SceneGeometrySources& sources)
        : sources(sources)
    {
    }

    void SceneGeometryCollector::collect(navigation::NavmeshInputGeometry& outGeometry) const
    {
        collectSources(nullptr, outGeometry);
    }

    void SceneGeometryCollector::collect(const navigation::NavmeshTileBounds& bounds,
                                         navigation::NavmeshInputGeometry& outGeometry) const
    {
        collectSources(&bounds, outGeometry);
    }

    void SceneGeometryCollector::collectSources(const navigation::NavmeshTileBounds* bounds,
                                                navigation::NavmeshInputGeometry& outGeometry) const
    {
        if (sources.terrain)
            collectTerrain(bounds, outGeometry);
        if (sources.staticMeshes)
            collectStaticMeshes(bounds, outGeometry);
        if (sources.colliders)
            collectColliders(bounds, outGeometry);
        if (sources.foliage)
            collectFoliage(bounds, outGeometry);
        if (sources.obstacles)
            collectObstacles(bounds, outGeometry);
    }
}
