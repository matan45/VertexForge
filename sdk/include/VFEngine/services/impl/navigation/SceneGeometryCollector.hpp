#pragma once

#include "navigation/NavmeshData.hpp"
#include "navigation/volumetric/VolumetricVoxelizer.hpp"
#include <glm/glm.hpp>

namespace services
{
    // Which world geometry a navigation bake rasterizes (VK-1660).
    struct SceneGeometrySources
    {
        bool terrain = true;
        bool staticMeshes = true;
        bool colliders = true; // Box / ConvexMesh / TriangleMesh colliders of entities without a mesh
        bool foliage = true;   // nav-contributing foliage types on resident terrain tiles (VK-1584)
        bool obstacles = true; // NavmeshObstacleComponent volumes in Carve mode
    };

    // VK-1660: gathers the static world geometry a navigation bake rasterizes, in WORLD space - a
    // parented entity is placed by its composed hierarchy matrix (scene::composeWorldMatrix), not by its
    // local transform. Shared by the navmesh bake and the volumetric bake. Movers never contribute
    // (NavmeshAgent, VolumetricAgent and Controller entities, Dynamic rigid bodies), nor do trigger
    // colliders. Main thread: reads EnTT and loads meshes synchronously.
    class SceneGeometryCollector
    {
    public:
        explicit SceneGeometryCollector(const SceneGeometrySources& sources);

        // Every contributing source in the scene.
        void collect(navigation::NavmeshInputGeometry& outGeometry) const;

        // Only what can overlap `bounds` on XZ (callers expand tile bounds for the border): terrain and
        // foliage by terrain tile, entities by world origin - meshes and colliders within 50 m,
        // obstacles within their extent.
        void collect(const navigation::NavmeshTileBounds& bounds, navigation::NavmeshInputGeometry& outGeometry) const;

        // VK-1660: the volumetric bake's snapshot of the box [boxMin, boxMax], selected on XZ like the bounded
        // collect (the voxelizer clips to the box): terrain triangles into out.terrainVertices / terrainIndices
        // (sources.terrain); mesh, foliage and Box / ConvexMesh / TriangleMesh collider triangles into
        // out.vertices / indices (sources.staticMeshes / foliage / colliders); Sphere / Capsule colliders - which
        // the navmesh does not rasterize - as solid shapes into out.spheres / capsules (sources.colliders). Carve
        // obstacles shape the navmesh only and are left out. Appends; the box and settings of `out` are untouched.
        void collectVolumetric(const glm::vec3& boxMin, const glm::vec3& boxMax,
                               volumetric::VolumetricBakeInput& out) const;

    private:
        SceneGeometrySources sources;

        // `bounds` null = the whole scene.
        void collectSources(const navigation::NavmeshTileBounds* bounds,
                            navigation::NavmeshInputGeometry& outGeometry) const;
    };
}
