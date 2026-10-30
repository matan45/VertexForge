#pragma once

#include "navigation/NavmeshData.hpp"

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

    private:
        SceneGeometrySources sources;

        // `bounds` null = the whole scene.
        void collectSources(const navigation::NavmeshTileBounds* bounds,
                            navigation::NavmeshInputGeometry& outGeometry) const;
    };
}
