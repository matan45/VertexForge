#pragma once

// VK-1433 Phase 1b — header-only, CPU-testable bone-pick math for the Prefab Rig Preview window.
//
// Kept header-only (like PrefabRigValidation.hpp / PrefabRigDescBuilder.hpp) so the Tests project
// can exercise it with NO imgui, NO Graphics, NO Vulkan and NO EventDispatcher. The window feeds in
// the skeletal part's world-space joints (queried across the preview boundary) plus the live camera
// view/projection, the viewport rect, and the click position; the helper returns the nearest joint
// to the click within a pixel threshold (or -1).
//
// Projection convention: the window's gizmo path renders with the Vulkan camera projection but undoes
// the Vulkan Y-flip for ImGuizmo (proj[1][1] *= -1, giving an OpenGL-style Y-up clip space). This
// helper applies the SAME flip so a picked screen point lines up with where ImGuizmo / the skeleton
// overlay actually draw the joint — a joint ABOVE the rig (higher world Y) lands in the UPPER half of
// the viewport, not mirrored. Behind-camera joints (clip.w <= 0) and joints projecting outside the
// viewport rect are rejected before the nearest-within-threshold test.

#include <glm/glm.hpp>
#include <cstddef>
#include <limits>
#include <vector>

namespace windows::prefabrigpick
{
    // Result of a screen-space joint pick.
    struct JointPickResult
    {
        int index = -1;                                              // nearest joint index, or -1 if none
        float screenDistance = std::numeric_limits<float>::max();    // pixels to that joint (max if none)

        bool hit() const { return index >= 0; }
    };

    // Project a single world point to viewport-pixel space using the gizmo's OpenGL-style convention
    // (the caller passes the Vulkan camera projection; this applies proj[1][1] *= -1 like the gizmo).
    // Returns false (and leaves outScreen untouched) when the point is behind the camera (w <= 0).
    inline bool projectJointToScreen(const glm::vec3& world, const glm::mat4& view, const glm::mat4& proj,
                                     const glm::vec2& viewportMin, const glm::vec2& viewportSize,
                                     glm::vec2& outScreen)
    {
        glm::mat4 glProj = proj;
        glProj[1][1] *= -1.0f; // match the ImGuizmo / overlay screen mapping (Y-up clip space)

        const glm::vec4 clip = glProj * view * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            return false; // behind (or on) the camera plane — not pickable

        const glm::vec3 ndc = glm::vec3(clip) / clip.w; // [-1,1]^3 (Y up)

        // NDC -> viewport pixels. Screen Y grows DOWN, so flip the Y-up NDC.
        const float sx = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        const float sy = viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;
        outScreen = glm::vec2(sx, sy);
        return true;
    }

    // Nearest joint to a click in viewport-pixel space, within pixelThreshold. Joints behind the
    // camera or projecting outside [viewportMin, viewportMin+viewportSize] are skipped. Returns the
    // winning index + its pixel distance, or {-1, max} when nothing qualifies.
    inline JointPickResult nearestJointToScreenPoint(const std::vector<glm::vec3>& jointWorlds,
                                                     const glm::mat4& view, const glm::mat4& proj,
                                                     const glm::vec2& viewportMin,
                                                     const glm::vec2& viewportSize,
                                                     const glm::vec2& clickPos, float pixelThreshold)
    {
        JointPickResult best;
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
            return best;

        const glm::vec2 viewportMax = viewportMin + viewportSize;

        for (size_t i = 0; i < jointWorlds.size(); ++i)
        {
            glm::vec2 screen;
            if (!projectJointToScreen(jointWorlds[i], view, proj, viewportMin, viewportSize, screen))
                continue; // behind camera

            // Reject joints that project outside the viewport (a click can only be inside it anyway).
            if (screen.x < viewportMin.x || screen.x > viewportMax.x ||
                screen.y < viewportMin.y || screen.y > viewportMax.y)
                continue;

            const float dist = glm::distance(screen, clickPos);
            if (dist <= pixelThreshold && dist < best.screenDistance)
            {
                best.index = static_cast<int>(i);
                best.screenDistance = dist;
            }
        }
        return best;
    }
}
