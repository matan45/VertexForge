#pragma once
#include "../asset/AssetRef.hpp"
#include "../terrain/SplineTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace components
{
    // VK-1621 — the authoring data behind a generated road, carried by the road's root entity.
    //
    // Splines themselves are session-only: SplineData lives in SplineTerrainServiceImpl and is not
    // serialized anywhere, and VK-1618 deliberately deferred a persistent terrain edit-layer
    // sidecar to VK-1644..1648 (docs/TERRAIN_EDIT_LAYERS.md). A ROAD's spline is different — it is
    // scene data, not terrain data — so it rides the entity and persists through normal scene
    // serialization, which is what makes "select the road, move a point, regenerate" survive a
    // reload without touching any of the deferred terrain work.
    //
    // Note what this does NOT restore: the terrain deformation the spline caused is still baked
    // into the height data and still not revertable across sessions. That remains VK-1644..1648.
    struct RoadSplineComponent
    {
        std::vector<glm::vec3> controlPoints;
        terrain::SplineParams params; // ops, corridor, road profile, name, material, collider flag

        asset::AssetRef generatedMeshRef;
        uint64_t splineId = 0;

        // Each regeneration writes a NEW .vfMesh (…_r2, _r3, …) instead of overwriting, because
        // there is no mesh cache invalidation (ResourceManager.hpp:89-92 has material and
        // terrain-material invalidators but no mesh one) and MeshStreamHandle keeps the file open
        // (MeshStreamHandle.hpp:70). Persisting the revision keeps the numbering monotonic across
        // sessions so a reloaded road never writes over the file it is currently rendering.
        uint32_t revision = 0;
        uint32_t chunkCount = 0;
    };
}
