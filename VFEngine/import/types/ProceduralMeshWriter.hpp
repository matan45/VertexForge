#pragma once

#include "../ImportExport.hpp"
#include "Mesh.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace types
{
    // One submesh of a procedurally generated static mesh, with its LOD chain supplied by the
    // caller rather than derived.
    //
    // The importer's own path builds LODs with meshopt_simplifySloppy (MeshLODGenerator.cpp:280 —
    // FLT_MAX tolerance, no border lock), which is right for arbitrary imported art and wrong for
    // generated geometry with a load-bearing silhouette: on a road ribbon it collapses the width
    // and pulls the edges off the terrain. It matters beyond looks, because the physics collider
    // reads LOD 2, not LOD 0 (PhysicsShapeFactory.cpp:168,222). Generators that know their own
    // topology can decimate losslessly, so this writer takes the chain as given.
    struct ProceduralSubmesh
    {
        std::string name;
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> lods;
    };

    struct ProceduralMeshWriteResult
    {
        bool success = false;
        std::string message;
        std::string outputPath;
        uint32_t submeshCount = 0;
        uint64_t totalVertices = 0;
    };

    class VF_IMPORT_API ProceduralMeshWriter
    {
    public:
        // Writes a static (non-skinned) .vfMesh from CPU vertex/index data. Byte-compatible with
        // the importer's output — same header (MeshFileLayout.hpp), same compressed LOD blocks,
        // meshlets and trailer — so everything downstream treats the result as an ordinary mesh
        // asset with no renderer changes.
        //
        // The convex-decomposition block is written empty (a single 0 byte the reader accepts), so
        // V-HACD never runs. Generated surfaces that need collision should use
        // ColliderShape::TriangleMesh, which reads the LOD chain directly.
        //
        // Writes to "<outputPath>.tmp" and renames on success, so a failure part-way through
        // cannot leave a truncated file where a valid asset used to be.
        static ProceduralMeshWriteResult write(const std::string& outputPath,
                                               const std::vector<ProceduralSubmesh>& submeshes);
    };
}
