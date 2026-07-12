#pragma once

#include "imgui.h"
#include "math/TransformUtils.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// VK-1443 — shared internal helpers for the PrefabPreviewWindow decomposition. These were
// previously file-local (anonymous namespace) inside PrefabPreviewWindow.cpp; once the window is
// split across several TUs (sandbox / hierarchy / authoring / undo sub-controllers) they must live
// in a header with external-linkage-safe `inline` so every TU sees the same definition (an
// anonymous namespace in a header would give each TU its own copy and defeat the purpose).
namespace windows::prefabdetail
{
    // De-hardcoded UI timings (formerly literal 3.0f / 0.25f sprinkled through the panels).
    inline constexpr float kSaveFeedbackSeconds = 3.0f;       // how long "Saved/Failed" stays on screen
    inline constexpr float kDefaultStateBlendSeconds = 0.25f; // default animator state-transition blend

    // --- NaN guards for the transform gizmo (VK-1433) -------------------------------------
    // The gizmo maps a manipulated WORLD matrix back to the part's own-local transform via two
    // matrix inversions; a degenerate (near-zero) scale or a pre-existing NaN would produce
    // inf/NaN that then gets persisted and fed back next frame (a sticky NaN). These let the
    // gizmo skip a bad frame instead of writing garbage.
    // Thin aliases over the shared math:: guards so the finite-check logic has a
    // single source of truth (a future hardening propagates to all callers).
    inline bool isFiniteVec(const glm::vec3& v) { return math::isFinite(v); }
    inline bool isFiniteMat(const glm::mat4& m) { return math::isFinite(m); }
    inline float minAbsComponent(const glm::vec3& v)
    {
        return std::min({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    }

    // Transient "Saved/Failed" badge shared by the prefab / socket / static-socket / IK save
    // buttons (each keeps its own timer + success flag; this only draws the badge). Green "Saved"
    // on success, red "Failed" otherwise, on the same line as the preceding button. No-op when the
    // timer has elapsed (timer <= 0).
    inline void drawSaveBadge(float timer, bool success)
    {
        if (timer <= 0.0f) return;
        ImGui::SameLine();
        ImGui::TextColored(success ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                           success ? "Saved" : "Failed");
    }
} // namespace windows::prefabdetail
