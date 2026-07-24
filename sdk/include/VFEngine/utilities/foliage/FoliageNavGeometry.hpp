#pragma once
// VK-1584 — pure, Vulkan-free transform-and-append of foliage geometry into the navmesh baker's
// input. The bake-time collector (NavmeshGeometryCollector::collectFoliageGeometryForBounds) loads
// each nav-contributing FoliageType's mesh ONCE (a coarse LOD), extracts its positions once, then
// calls appendFoliageInstanceGeometry for every instance of that type — never a per-instance load.
// Kept header-only (FoliageCompose + NavmeshData + glm) so the transform math is CPU-testable by
// the doctest Tests target with a stub mesh, no Recast/services/graphics dependency.
#include "FoliageCompose.hpp"                 // composeFoliageModelMatrix, FoliageInstance/FoliageType
#include "../navigation/NavmeshData.hpp"      // navigation::NavmeshInputGeometry
#include <glm/glm.hpp>
#include <span>
#include <cstdint>

namespace foliage
{
    // Transform a triangle mesh (world-space positions + index triplets) by `model` and append it
    // to the flat navmesh input, offsetting indices by the current vertex count so multiple meshes
    // concatenate correctly. Trailing indices that don't complete a triangle are ignored.
    inline void appendTransformedMesh(const glm::mat4& model,
                                      std::span<const glm::vec3> positions,
                                      std::span<const uint32_t> indices,
                                      navigation::NavmeshInputGeometry& out)
    {
        const int baseVertex = out.getVertexCount();
        for (const glm::vec3& p : positions)
            out.addVertex(glm::vec3(model * glm::vec4(p, 1.0f)));

        for (std::size_t i = 0; i + 3 <= indices.size(); i += 3)
            out.addTriangle(baseVertex + static_cast<int>(indices[i]),
                            baseVertex + static_cast<int>(indices[i + 1]),
                            baseVertex + static_cast<int>(indices[i + 2]));
    }

    // Append one foliage instance's coarse triangles, transformed to world space by the instance's
    // TRS (reusing the SAME matrix the renderer builds via composeFoliageModelMatrix), so the nav
    // geometry lines up exactly with the rendered plants.
    inline void appendFoliageInstanceGeometry(const FoliageInstance& inst,
                                              const FoliageType& type,
                                              std::span<const glm::vec3> positions,
                                              std::span<const uint32_t> indices,
                                              navigation::NavmeshInputGeometry& out)
    {
        appendTransformedMesh(composeFoliageModelMatrix(inst, type), positions, indices, out);
    }
}
