#include "NavmeshServiceImpl.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"

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

            const auto& mesh = view.get<components::MeshComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            if (mesh.meshPath.empty())
            {
                continue;
            }

            auto meshFuture = resource::ResourceManager::loadMeshAsync(mesh.meshPath);
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
                if (collider.meshPath.empty())
                {
                    continue;
                }

                auto meshFuture = resource::ResourceManager::loadMeshAsync(collider.meshPath);
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
}
