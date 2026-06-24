#pragma once

// VK-1433 Phase 1 — Prefab Rig Preview overlay geometry (pure, CPU-testable).
//
// Builds the debug-line geometry (skeleton bones, socket axis triads, IK target markers)
// for the rig visualization overlay. The functions are free + pure: they take already-
// resolved world-space inputs (joint worlds, socket worlds, IK endpoints) and append
// render::mesh::DebugLineVertex segments to an ImmediateDebugDrawList. No Vulkan, no
// assembly, no assets — so they unit-test on the CPU (see tests/test_prefab_rig_overlay.cpp).
//
// The PrefabRigPreviewController owns the per-frame glue: it pulls the live post-IK bone
// matrices / part worlds / sockets from PrefabRigAssembly, applies the verified prefabrig::*
// formulas to get world transforms, and calls these to emit the lines.

#include "../../render/tools/ImmediateDebugTypes.hpp"

#include <glm/glm.hpp>

namespace controllers::prefabrigoverlay
{
    // A short distinct palette so each part's skeleton (body vs weapon) reads differently.
    glm::vec4 partColor(size_t partIndex);

    // Single colored segment a->b.
    void addLine(render::mesh::ImmediateDebugDrawList& out,
                 const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

    // RGB axis triad at `transform`'s origin (X=red, Y=green, Z=blue), each `axisLength` long
    // in `transform`'s local space. Used to mark socket frames (orientation matters there).
    void addAxisTriad(render::mesh::ImmediateDebugDrawList& out,
                      const glm::mat4& transform, float axisLength);

    // Small 3-axis cross centered at `position` (no orientation), `halfSize` per arm. Used to
    // mark joints / IK targets where only position matters.
    void addMarker(render::mesh::ImmediateDebugDrawList& out,
                   const glm::vec3& position, float halfSize, const glm::vec4& color);
}
