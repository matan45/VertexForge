#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace render::upscaling
{
    // VK-1662: the pure math behind what DLSS (through Streamline) is fed - the motion vectors, the reprojection
    // matrices and the camera constants. Header-only and Vulkan-free so the CPU-only Tests project
    // (test_upscale_motion_vectors) pins the conventions down. MotionVectorPass, RenderPassHandler and
    // UpscaleManager all call into this file; motionVectorNdc has a GPU twin in
    // resources/shaders/common/motion_vectors.glsl that must stay identical to it.
    //
    // Conventions (DLSS SR Programming Guide 310.6.0 §3.6-3.7, Streamline sl_consts.h):
    //  * A motion vector maps a pixel's CURRENT position to where it was in the PREVIOUS frame (previous - current).
    //    It is stored in NDC with +y down (Vulkan, positive-height viewports); UpscaleManager's mvecScale = 0.5
    //    converts NDC to UV and Streamline multiplies by the render size, giving the render pixels DLSS wants.
    //  * Neither end contains the sub-pixel jitter (motionVectorsJittered = false). The depth buffer was rasterised
    //    with the JITTERED projection, so a pixel centre plus its depth is a point in jittered clip space; both ends
    //    are re-projected from there with UNJITTERED projections.
    //  * Streamline's camera matrices are jitter-free too, and Streamline uses row-vector math with row-major
    //    storage - byte-identical to GLM's column-vector, column-major memory (toStreamlineMatrix).

    // Maps a point from the source camera's clip space to the target camera's clip space:
    // targetProjection * (targetView * inverse(sourceView)) * inverse(sourceProjection).
    // Composed in double, with the relative view transform formed first, so large world translations cancel
    // before anything is rounded to float - the idea behind Streamline's calcCameraToPrevCamera
    // (sl_matrix_helpers.h). Used for the motion vectors and for Streamline's clipToPrevClip / prevClipToClip.
    inline glm::mat4 reprojectionMatrix(const glm::mat4& targetProjection, const glm::mat4& targetView,
                                        const glm::mat4& sourceView, const glm::mat4& sourceProjection)
    {
        const glm::dmat4 relativeView = glm::dmat4(targetView) * glm::inverse(glm::dmat4(sourceView));
        return glm::mat4(glm::dmat4(targetProjection) * relativeView * glm::inverse(glm::dmat4(sourceProjection)));
    }

    // The two matrices the motion-vector shader applies to a jittered clip-space point (pixel-centre NDC, depth, 1).
    // Both are built by the same function from the same source camera, so for a static camera (prevView == view and
    // prevUnjitteredProjection == unjitteredProjection) they are bitwise equal and every motion vector is exactly
    // zero, whatever the jitter.
    struct MotionVectorMatrices
    {
        glm::mat4 jitteredToCurrentClip{1.0f};  // P_unjittered * inverse(P_jittered)
        glm::mat4 jitteredToPreviousClip{1.0f}; // P_prev,unjittered * V_prev * inverse(V) * inverse(P_jittered)
    };

    inline MotionVectorMatrices buildMotionVectorMatrices(const glm::mat4& view,
                                                          const glm::mat4& jitteredProjection,
                                                          const glm::mat4& unjitteredProjection,
                                                          const glm::mat4& prevView,
                                                          const glm::mat4& prevUnjitteredProjection)
    {
        MotionVectorMatrices matrices;
        matrices.jitteredToCurrentClip = reprojectionMatrix(unjitteredProjection, view, view, jitteredProjection);
        matrices.jitteredToPreviousClip =
            reprojectionMatrix(prevUnjitteredProjection, prevView, view, jitteredProjection);
        return matrices;
    }

    // Largest motion-vector component written, in NDC (MV_MAX_NDC in motion_vectors.glsl). A pixel's current NDC lies
    // in [-1, 1] on each axis, so a component of magnitude 4 puts the previous position at least 3 away on that axis -
    // off-screen from anywhere on screen. Clamping there keeps the on/off-screen classification exact while keeping
    // the rg16f target finite when the previous clip w approaches zero (a point just in front of the previous
    // camera's eye plane). (max, max) also marks "no history" (see motionVectorNdc).
    inline constexpr float maxMotionVectorNdc = 4.0f;

    // CPU twin of motionVectorNdc in resources/shaders/common/motion_vectors.glsl - keep the two identical.
    inline glm::vec2 motionVectorNdc(const glm::vec4& currentClip, const glm::vec4& previousClip)
    {
        // On or behind the previous camera's eye plane the point had no previous position on screen: point
        // off-screen so DLSS rejects the history, exactly as for a point just in front of that plane (which clamps).
        if (previousClip.w <= 0.0f)
            return glm::vec2(maxMotionVectorNdc);
        const glm::vec2 motion = glm::vec2(previousClip) / previousClip.w - glm::vec2(currentClip) / currentClip.w;
        return glm::clamp(motion, glm::vec2(-maxMotionVectorNdc), glm::vec2(maxMotionVectorNdc));
    }

    // CPU reference of resources/shaders/postprocess/motion_vectors_fullscreen.glsl for the pixel centre at uv
    // ([0, 1], +y down) and its depth-buffer value.
    inline glm::vec2 referenceMotionVector(const MotionVectorMatrices& matrices, const glm::vec2& uv, float depth)
    {
        const glm::vec4 jitteredClip(uv * 2.0f - 1.0f, depth, 1.0f);
        return motionVectorNdc(matrices.jitteredToCurrentClip * jitteredClip,
                               matrices.jitteredToPreviousClip * jitteredClip);
    }

    // Vertical field of view in radians; positive even for the engine's Y-flipped projections ([1][1] < 0).
    inline float verticalFov(const glm::mat4& projection)
    {
        return 2.0f * std::atan(1.0f / std::abs(projection[1][1]));
    }

    // glm::ortho keeps clip w = 1; glm::perspective writes -1 into [2][3]. JitterSequence::applyJitter never
    // touches either entry, so this holds for jittered projections too.
    inline bool isOrthographic(const glm::mat4& projection)
    {
        return projection[2][3] == 0.0f && projection[3][3] == 1.0f;
    }

    // Streamline's sl::float4x4 is row-major AND uses row-vector math (p' = p * S): translation lives in row 3 and
    // clipToPrevClip = clipToView * viewToViewPrev * viewToClipPrev (sl_consts.h, sl_matrix_helpers.h). For a GLM
    // matrix M (column vectors, column-major) the Streamline matrix is S = transpose(M), whose row i is GLM's
    // column i - the same bytes. So this is a straight copy, never a transpose. Element [4 * row + column] of S.
    inline std::array<float, 16> toStreamlineMatrix(const glm::mat4& matrix)
    {
        std::array<float, 16> rows{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
                rows[4 * i + j] = matrix[static_cast<glm::length_t>(i)][static_cast<glm::length_t>(j)];
        }
        return rows;
    }
}
