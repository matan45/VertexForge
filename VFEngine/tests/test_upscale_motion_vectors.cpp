#include <doctest.h>
#include <render/postprocess/JitterSequence.hpp>
#include <render/upscaling/ResolutionManager.hpp>
#include <render/upscaling/TemporalUpscaleMath.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

// ============================================================
// VK-1662: what DLSS (through Streamline) is fed. No Vulkan, so the CPU-only Tests project pins down:
//   * the jitter: applyJitter moves the image by +jitterOffset render pixels (+y down) at every depth, for
//     perspective AND ortho projections, and the Halton phase count follows DLSS's 8 * (display/render)^2;
//   * the motion vectors (CPU twin of motion_vectors_fullscreen.glsl): previous - current, jitter-free - exactly
//     zero for a static camera whatever the jitter, the right sign for yaw / strafe (perspective and ortho), exact
//     for the sky under rotation, precise far from the origin, and off-screen (clamped, or the (4, 4) marker) where
//     no previous position exists - plus a string guard that the GLSL twin still says the same;
//   * Streamline's camera constants: clipToPrevClip / prevClipToClip, the vertical FOV, the ortho flag, and the
//     row-vector matrix layout (a straight copy of GLM's memory, never a transpose).
// The engine's projections are GLM right-handed with depth zero-to-one (GLM_FORCE_DEPTH_ZERO_TO_ONE is a
// workspace-wide define) and Y flipped for Vulkan; its viewports have a positive height, so NDC +y is down.
// ============================================================

using render::postprocess::JitterSequence;
using render::upscaling::MotionVectorMatrices;
using render::upscaling::buildMotionVectorMatrices;
using render::upscaling::isOrthographic;
using render::upscaling::maxMotionVectorNdc;
using render::upscaling::referenceMotionVector;
using render::upscaling::reprojectionMatrix;
using render::upscaling::toStreamlineMatrix;
using render::upscaling::verticalFov;

namespace
{
    constexpr uint32_t renderWidth = 1280;
    constexpr uint32_t renderHeight = 720;
    const glm::vec2 renderSize{1280.0f, 720.0f};

    constexpr float pixelTolerance = 1e-3f;

    // EditorCamera / CameraComponent: glm::perspective, then [1][1] flipped for Vulkan.
    glm::mat4 enginePerspective()
    {
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), renderSize.x / renderSize.y, 0.1f, 2000.0f);
        projection[1][1] *= -1.0f;
        return projection;
    }

    glm::mat4 engineOrtho()
    {
        glm::mat4 projection = glm::ortho(-12.8f, 12.8f, -7.2f, 7.2f, 0.1f, 2000.0f);
        projection[1][1] *= -1.0f;
        return projection;
    }

    // A right-handed view (the camera looks down -Z). yawRadians > 0 turns the camera to the RIGHT: its forward
    // swings from -Z toward +X.
    glm::mat4 viewAt(const glm::vec3& position, float yawRadians)
    {
        const glm::vec3 forward(std::sin(yawRadians), 0.0f, -std::cos(yawRadians));
        return glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    struct Camera
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f}; // unjittered
    };

    // Render-pixel position (+x right, +y down, as a positive-height Vulkan viewport maps NDC) of a clip point.
    glm::vec2 clipToPixels(const glm::vec4& clip)
    {
        return (glm::vec2(clip) / clip.w * 0.5f + 0.5f) * renderSize;
    }

    glm::vec2 projectToPixels(const glm::mat4& projection, const glm::vec3& viewPoint)
    {
        return clipToPixels(projection * glm::vec4(viewPoint, 1.0f));
    }

    // The motion vector (NDC) the pass writes where the current frame - rasterised through `jitter` render px -
    // shows worldPoint.
    glm::vec2 motionVectorNdcAt(const Camera& current, const Camera& previous, const glm::vec2& jitter,
                                const glm::vec3& worldPoint)
    {
        const glm::mat4 jitteredProjection =
            JitterSequence::applyJitter(current.projection, jitter, renderWidth, renderHeight);
        const glm::vec4 jitteredClip = jitteredProjection * current.view * glm::vec4(worldPoint, 1.0f);
        const glm::vec3 ndc = glm::vec3(jitteredClip) / jitteredClip.w;
        const MotionVectorMatrices matrices = buildMotionVectorMatrices(
            current.view, jitteredProjection, current.projection, previous.view, previous.projection);
        return referenceMotionVector(matrices, glm::vec2(ndc) * 0.5f + 0.5f, ndc.z);
    }

    glm::vec2 motionVectorPixelsAt(const Camera& current, const Camera& previous, const glm::vec2& jitter,
                                   const glm::vec3& worldPoint)
    {
        return motionVectorNdcAt(current, previous, jitter, worldPoint) * 0.5f * renderSize;
    }
}

TEST_SUITE("UpscaleMotionVectors")
{
    TEST_CASE("perspective jitter moves the image by +jitterOffset render pixels at every depth")
    {
        const glm::mat4 projection = enginePerspective();
        const glm::vec2 jitter(0.25f, -0.1f);
        const glm::mat4 jittered = JitterSequence::applyJitter(projection, jitter, renderWidth, renderHeight);

        // t = 2 * jitter / extent through the w row (w = -z): [2][0] -= t.x, [2][1] -= t.y, nothing else changes.
        // (Absolute checks: doctest::Approx's absolute floor of ~1e-5 is too coarse for values of ~4e-4.)
        CHECK(std::abs(jittered[2][0] - (-2.0f * 0.25f / 1280.0f)) < 1e-9f);
        CHECK(std::abs(jittered[2][1] - (2.0f * 0.1f / 720.0f)) < 1e-9f);
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                if (column == 2 && row < 2)
                    continue;
                CHECK(jittered[column][row] == projection[column][row]);
            }
        }

        for (const float z : {-0.5f, -10.0f, -1500.0f})
        {
            const glm::vec3 viewPoint(-0.2f * z, 0.1f * z, z); // inside the frustum at every depth
            const glm::vec2 moved = projectToPixels(jittered, viewPoint) - projectToPixels(projection, viewPoint);
            CHECK(std::abs(moved.x - 0.25f) < pixelTolerance);
            CHECK(std::abs(moved.y - (-0.1f)) < pixelTolerance);
        }
    }

    TEST_CASE("ortho jitter moves the image by +jitterOffset render pixels with no depth shear")
    {
        const glm::mat4 projection = engineOrtho();
        const glm::vec2 jitter(0.25f, -0.1f);
        const glm::mat4 jittered = JitterSequence::applyJitter(projection, jitter, renderWidth, renderHeight);

        // w = 1: the offset lands in the translation column, never in the z column (the old depth shear).
        CHECK(jittered[2][0] == projection[2][0]);
        CHECK(jittered[2][1] == projection[2][1]);
        CHECK(std::abs(jittered[3][0] - (projection[3][0] + 2.0f * 0.25f / 1280.0f)) < 1e-9f);
        CHECK(std::abs(jittered[3][1] - (projection[3][1] - 2.0f * 0.1f / 720.0f)) < 1e-9f);

        for (const float z : {-1.0f, -100.0f, -1999.0f})
        {
            const glm::vec3 viewPoint(3.0f, -2.0f, z);
            const glm::vec2 moved = projectToPixels(jittered, viewPoint) - projectToPixels(projection, viewPoint);
            CHECK(std::abs(moved.x - 0.25f) < pixelTolerance);
            CHECK(std::abs(moved.y - (-0.1f)) < pixelTolerance);
        }
    }

    TEST_CASE("zero jitter or a zero extent leaves the projection unchanged")
    {
        const glm::mat4 projection = enginePerspective();
        const glm::mat4 noJitter = JitterSequence::applyJitter(projection, glm::vec2(0.0f), renderWidth, renderHeight);
        const glm::mat4 noExtent = JitterSequence::applyJitter(projection, glm::vec2(0.3f, 0.2f), 0, renderHeight);
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                CHECK(noJitter[column][row] == projection[column][row]);
                CHECK(noExtent[column][row] == projection[column][row]);
            }
        }
    }

    TEST_CASE("Halton phase count follows DLSS's 8 * (display / render)^2 for every quality mode")
    {
        struct Expected
        {
            ::postprocess::UpscaleQuality quality;
            uint32_t phases;
        };
        // Minimums from the DLSS SR Programming Guide §3.7.1.1, at the render sizes ResolutionManager really picks.
        const std::array<Expected, 5> expected{{
            {::postprocess::UpscaleQuality::Native, 8},           // DLAA, 1920x1080
            {::postprocess::UpscaleQuality::Quality, 18},         // 1280x720
            {::postprocess::UpscaleQuality::Balanced, 24},        // 1130x636 (8 * 2.885 = 23.08)
            {::postprocess::UpscaleQuality::Performance, 32},     // 960x540
            {::postprocess::UpscaleQuality::UltraPerformance, 72} // 640x360
        }};
        for (const Expected& mode : expected)
        {
            render::upscaling::ResolutionManager resolution;
            resolution.setDisplayResolution(1920, 1080);
            resolution.setQualityMode(mode.quality);
            const vk::Extent2D renderExtent = resolution.getRenderResolution();
            CHECK(JitterSequence::phaseCount(renderExtent.width, renderExtent.height, 1920, 1080) == mode.phases);
        }

        // An exact ratio does not round up through float noise; an inexact one rounds up (more phases is fine).
        CHECK(JitterSequence::phaseCount(1280, 720, 1920, 1080) == 18);
        CHECK(JitterSequence::phaseCount(1706, 960, 2560, 1440) == 19); // 2560x1440 Quality: 8 * 2.2509 = 18.007
        CHECK(JitterSequence::phaseCount(0, 0, 1920, 1080) == 8);
    }

    TEST_CASE("a static camera gives exactly zero motion whatever the jitter, sky included")
    {
        const glm::mat4 projection = enginePerspective();
        const glm::mat4 view = viewAt(glm::vec3(1.0f, 2.0f, 3.0f), 0.3f);
        const glm::mat4 jitteredProjection =
            JitterSequence::applyJitter(projection, glm::vec2(0.3f, -0.2f), renderWidth, renderHeight);
        const MotionVectorMatrices matrices =
            buildMotionVectorMatrices(view, jitteredProjection, projection, view, projection);

        for (const float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            for (const float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                for (const float depth : {0.0f, 0.5f, 0.999f, 1.0f})
                {
                    const glm::vec2 mv = referenceMotionVector(matrices, glm::vec2(u, v), depth);
                    CHECK(mv.x == 0.0f);
                    CHECK(mv.y == 0.0f);
                }
            }
        }
    }

    TEST_CASE("turning the camera right makes static geometry's vectors point right (to the previous position)")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera previous{viewAt(glm::vec3(0.0f), glm::radians(-1.0f)), projection};
        const Camera current{viewAt(glm::vec3(0.0f), 0.0f), projection};

        // A point straight ahead now sat sx * tan(1 deg) NDC right of the previous centre: 640 * 0.0170061 = +10.884 px.
        const float expectedPx = 0.5f * renderSize.x * projection[0][0] * std::tan(glm::radians(1.0f));
        const glm::vec2 mv = motionVectorPixelsAt(current, previous, glm::vec2(0.0f), glm::vec3(0.0f, 0.0f, -10.0f));
        CHECK(expectedPx > 10.8f); // the sign is the point of this case: +x, toward the previous position
        CHECK(std::abs(mv.x - expectedPx) < pixelTolerance);
        CHECK(std::abs(mv.y) < pixelTolerance);
    }

    TEST_CASE("strafing right gives depth-dependent vectors pointing right")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera previous{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const Camera current{viewAt(glm::vec3(0.1f, 0.0f, 0.0f), 0.0f), projection};

        // sx * 0.1 / depth NDC: +6.2354 px at depth 10, +0.6235 px at depth 100.
        const float pixelsPerUnitAtUnitDepth = 0.5f * renderSize.x * projection[0][0] * 0.1f;
        const glm::vec2 closeMv =
            motionVectorPixelsAt(current, previous, glm::vec2(0.0f), glm::vec3(0.1f, 0.0f, -10.0f));
        const glm::vec2 distantMv =
            motionVectorPixelsAt(current, previous, glm::vec2(0.0f), glm::vec3(0.1f, 0.0f, -100.0f));
        CHECK(std::abs(closeMv.x - pixelsPerUnitAtUnitDepth / 10.0f) < pixelTolerance);
        CHECK(std::abs(distantMv.x - pixelsPerUnitAtUnitDepth / 100.0f) < pixelTolerance);
        CHECK(closeMv.x > 6.2f);
        CHECK(std::abs(closeMv.y) < pixelTolerance);
        CHECK(std::abs(distantMv.y) < pixelTolerance);
    }

    TEST_CASE("ortho camera: strafing moves every depth by the same vector, jitter removed")
    {
        const glm::mat4 projection = engineOrtho();
        const Camera previous{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const Camera current{viewAt(glm::vec3(0.1f, 0.0f, 0.0f), 0.0f), projection};

        // No perspective divide (clip w = 1 on both frames): 0.1 units = P00 * 0.1 NDC = 640 * (2 / 25.6) * 0.1 = +5 px.
        const float expectedPx = 0.5f * renderSize.x * projection[0][0] * 0.1f;
        for (const float depth : {1.0f, 50.0f, 1500.0f})
        {
            const glm::vec2 mv =
                motionVectorPixelsAt(current, previous, glm::vec2(0.3f, -0.4f), glm::vec3(1.1f, -0.5f, -depth));
            CHECK(std::abs(mv.x - expectedPx) < pixelTolerance);
            CHECK(std::abs(mv.y) < pixelTolerance);
        }
        CHECK(expectedPx == doctest::Approx(5.0));
    }

    TEST_CASE("sky pixels (depth 1) move with camera rotation and almost not with translation")
    {
        const glm::mat4 projection = enginePerspective();
        const glm::vec2 centre(0.5f, 0.5f);

        const Camera turnedFrom{viewAt(glm::vec3(0.0f), glm::radians(-1.0f)), projection};
        const Camera turned{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const MotionVectorMatrices rotation =
            buildMotionVectorMatrices(turned.view, projection, projection, turnedFrom.view, projection);
        const glm::vec2 rotationMv = referenceMotionVector(rotation, centre, 1.0f) * 0.5f * renderSize;
        // The same as at any depth (rotation is depth-free): sx * tan(1 deg) NDC = +10.884 px.
        const float expectedRotationPx = 0.5f * renderSize.x * projection[0][0] * std::tan(glm::radians(1.0f));
        CHECK(std::abs(rotationMv.x - expectedRotationPx) < pixelTolerance);
        CHECK(std::abs(rotationMv.y) < pixelTolerance);

        const Camera strafedFrom{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const Camera strafed{viewAt(glm::vec3(0.1f, 0.0f, 0.0f), 0.0f), projection};
        const MotionVectorMatrices translation =
            buildMotionVectorMatrices(strafed.view, projection, projection, strafedFrom.view, projection);
        const glm::vec2 translationMv = referenceMotionVector(translation, centre, 1.0f) * 0.5f * renderSize;
        // sx * 0.1 / far NDC. In fp32 the depth-1 plane of this projection sits at ~2002 (1 + [2][2] cancels), so
        // ~0.0311 px rather than the 0.0312 a far plane of exactly 2000 would give.
        CHECK(std::abs(translationMv.x - 0.0311f) < pixelTolerance);
        CHECK(std::abs(translationMv.y) < pixelTolerance);
    }

    TEST_CASE("round trip: vector * 0.5 * render size = previous unjittered pixel - current unjittered pixel")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera previous{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const Camera current{viewAt(glm::vec3(0.05f, 0.02f, -0.1f), glm::radians(0.7f)), projection};
        const glm::vec2 jitter(0.3f, -0.4f);
        const glm::mat4 jitteredProjection =
            JitterSequence::applyJitter(projection, jitter, renderWidth, renderHeight);
        const MotionVectorMatrices matrices = buildMotionVectorMatrices(
            current.view, jitteredProjection, current.projection, previous.view, previous.projection);

        for (const glm::vec3& world : {glm::vec3(0.4f, -0.3f, -2.0f), glm::vec3(3.0f, 2.0f, -20.0f),
                                       glm::vec3(-30.0f, 15.0f, -200.0f)})
        {
            const glm::vec4 jitteredClip = jitteredProjection * current.view * glm::vec4(world, 1.0f);
            const glm::vec2 jitteredPx = clipToPixels(jitteredClip);
            const glm::vec2 currentPx = clipToPixels(current.projection * current.view * glm::vec4(world, 1.0f));
            const glm::vec2 previousPx = clipToPixels(previous.projection * previous.view * glm::vec4(world, 1.0f));

            // The jittered rasterisation drew the point `jitter` pixels away from its unjittered position.
            CHECK(std::abs(jitteredPx.x - jitter.x - currentPx.x) < pixelTolerance);
            CHECK(std::abs(jitteredPx.y - jitter.y - currentPx.y) < pixelTolerance);

            const glm::vec2 mvPx = referenceMotionVector(matrices, jitteredPx / renderSize,
                                                         jitteredClip.z / jitteredClip.w) * 0.5f * renderSize;
            CHECK(std::abs(mvPx.x - (previousPx.x - currentPx.x)) < pixelTolerance);
            CHECK(std::abs(mvPx.y - (previousPx.y - currentPx.y)) < pixelTolerance);
        }
    }

    TEST_CASE("clipToPrevClip maps a current clip point to its previous clip point and prevClipToClip inverts it")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera previous{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const Camera current{viewAt(glm::vec3(0.3f, 0.1f, -0.2f), glm::radians(2.0f)), projection};

        const glm::mat4 clipToPrevClip =
            reprojectionMatrix(previous.projection, previous.view, current.view, current.projection);
        const glm::mat4 prevClipToClip =
            reprojectionMatrix(current.projection, current.view, previous.view, previous.projection);

        for (const glm::vec3& world : {glm::vec3(0.5f, -0.2f, -3.0f), glm::vec3(-4.0f, 2.0f, -40.0f)})
        {
            const glm::vec4 currentClip = current.projection * current.view * glm::vec4(world, 1.0f);
            const glm::vec4 previousClip = previous.projection * previous.view * glm::vec4(world, 1.0f);
            const glm::vec4 mapped = clipToPrevClip * currentClip;
            CHECK(std::abs(mapped.x / mapped.w - previousClip.x / previousClip.w) < 1e-5f);
            CHECK(std::abs(mapped.y / mapped.w - previousClip.y / previousClip.w) < 1e-5f);
            CHECK(std::abs(mapped.z / mapped.w - previousClip.z / previousClip.w) < 1e-5f);
        }

        const glm::mat4 identity = prevClipToClip * clipToPrevClip;
        for (glm::length_t column = 0; column < 4; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
                CHECK(std::abs(identity[column][row] - (column == row ? 1.0f : 0.0f)) < 1e-4f);
        }
    }

    TEST_CASE("far from the origin the vectors match a double-precision reference")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera previous{viewAt(glm::vec3(1.0e4f, 0.0f, 1.0e4f), 0.0f), projection};
        const Camera current{viewAt(glm::vec3(1.0e4f + 0.1f, 0.0f, 1.0e4f), glm::radians(0.5f)), projection};

        // The reference uses the SAME float camera matrices in double and an exact world point, so the float
        // pipeline (camera-relative, composed in double) may only add rounding on top of them.
        const glm::dvec4 world(1.0e4 + 2.0, 1.0, 1.0e4 - 10.0, 1.0);
        const glm::dvec4 currentClip = glm::dmat4(current.projection) * glm::dmat4(current.view) * world;
        const glm::dvec4 previousClip = glm::dmat4(previous.projection) * glm::dmat4(previous.view) * world;
        const glm::dvec2 currentNdc = glm::dvec2(currentClip) / currentClip.w;
        const glm::dvec2 expectedPx =
            (glm::dvec2(previousClip) / previousClip.w - currentNdc) * 0.5 * glm::dvec2(1280.0, 720.0);

        const MotionVectorMatrices matrices = buildMotionVectorMatrices(
            current.view, current.projection, current.projection, previous.view, previous.projection);
        const glm::vec2 mvPx = referenceMotionVector(matrices, glm::vec2(currentNdc * 0.5 + 0.5),
                                                     static_cast<float>(currentClip.z / currentClip.w))
                             * 0.5f * renderSize;
        CHECK(std::abs(mvPx.x - static_cast<float>(expectedPx.x)) < pixelTolerance);
        CHECK(std::abs(mvPx.y - static_cast<float>(expectedPx.y)) < pixelTolerance);
    }

    TEST_CASE("no previous position on screen reads as off-screen: clamped, or the (4, 4) marker behind the camera")
    {
        const glm::mat4 projection = enginePerspective();
        const Camera current{viewAt(glm::vec3(0.0f), 0.0f), projection};
        const glm::vec3 world(1.0f, 0.0f, -10.0f);

        // The point sat only 0.01 in front of the previous camera's eye plane (inside its near plane): its previous
        // x_ndc is ~ sx * 1 / 0.01 ~ 97, so the vector clamps to the off-screen bound.
        const Camera almostAtPoint{viewAt(glm::vec3(0.0f, 0.0f, -9.99f), 0.0f), projection};
        const glm::vec2 clamped = motionVectorNdcAt(current, almostAtPoint, glm::vec2(0.0f), world);
        CHECK(std::isfinite(clamped.x));
        CHECK(clamped.x == maxMotionVectorNdc);
        CHECK(std::abs(clamped.y) <= 1e-6f);

        // The point lies behind the previous camera: no previous position on screen, so the same off-screen answer
        // (DLSS rejects the history) instead of a zero vector, which would claim "static, reuse this pixel".
        const Camera pastPoint{viewAt(glm::vec3(0.0f, 0.0f, -10.5f), 0.0f), projection};
        const glm::vec2 none = motionVectorNdcAt(current, pastPoint, glm::vec2(0.0f), world);
        CHECK(none.x == maxMotionVectorNdc);
        CHECK(none.y == maxMotionVectorNdc);
    }

    TEST_CASE("vertical FOV is positive for the Y-flipped projection; ortho is detected with or without jitter")
    {
        CHECK(verticalFov(enginePerspective()) == doctest::Approx(1.0471976));
        CHECK(isOrthographic(engineOrtho()));
        CHECK_FALSE(isOrthographic(enginePerspective()));

        const glm::vec2 jitter(0.3f, -0.4f);
        CHECK(isOrthographic(JitterSequence::applyJitter(engineOrtho(), jitter, renderWidth, renderHeight)));
        CHECK_FALSE(isOrthographic(JitterSequence::applyJitter(enginePerspective(), jitter, renderWidth, renderHeight)));
    }

    TEST_CASE("Streamline matrices are row-vector + row-major: a straight copy of GLM's memory")
    {
        // Row-vector math: p' = p * S, i.e. p'[j] = sum_i p[i] * S[4 * i + j], must reproduce GLM's M * p.
        const glm::mat4 matrix = enginePerspective() * viewAt(glm::vec3(1.0f, 2.0f, 3.0f), 0.4f);
        const std::array<float, 16> streamline = toStreamlineMatrix(matrix);
        const glm::vec4 point(1.5f, -2.0f, -7.0f, 1.0f);
        const glm::vec4 expected = matrix * point;
        for (std::size_t j = 0; j < 4; ++j)
        {
            float rowVectorProduct = 0.0f;
            for (std::size_t i = 0; i < 4; ++i)
                rowVectorProduct += point[static_cast<glm::length_t>(i)] * streamline[4 * i + j];
            CHECK(rowVectorProduct == doctest::Approx(expected[static_cast<glm::length_t>(j)]).epsilon(1e-5));
        }

        // The perspective w term (-1) sits in row 2 / column 3 and the depth translation in row 3 / column 2; the
        // old transposing conversion swapped them.
        const glm::mat4 projection = enginePerspective();
        const std::array<float, 16> streamlineProjection = toStreamlineMatrix(projection);
        CHECK(streamlineProjection[4 * 2 + 3] == -1.0f);
        CHECK(streamlineProjection[4 * 3 + 2] == projection[3][2]);
        CHECK(projection[3][2] != -1.0f);

        // A translation lands in row 3, where Streamline's sl_matrix_helpers.h keeps it.
        const std::array<float, 16> translation = toStreamlineMatrix(glm::translate(glm::mat4(1.0f),
                                                                                    glm::vec3(5.0f, 6.0f, 7.0f)));
        CHECK(translation[12] == 5.0f);
        CHECK(translation[13] == 6.0f);
        CHECK(translation[14] == 7.0f);
        CHECK(translation[15] == 1.0f);
        CHECK(translation[3] == 0.0f);
    }

    TEST_CASE("the GLSL twin still states the convention the CPU reference implements")
    {
        // Tests.exe runs from bin/Tests/<Config>/x64/ with no resources/ copy, so reach the source tree from this file
        // (as test_default_font_generation does). A failure means motion_vectors.glsl drifted from
        // render::upscaling::motionVectorNdc: change both together.
        const std::filesystem::path shaderPath = std::filesystem::path(__FILE__).parent_path().parent_path()
                                                     .parent_path() / "resources" / "shaders" / "common"
                                                 / "motion_vectors.glsl";
        std::ifstream file(shaderPath);
        REQUIRE(file.is_open());
        const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        CHECK(maxMotionVectorNdc == 4.0f);
        CHECK(source.find("const float MV_MAX_NDC = 4.0;") != std::string::npos);
        CHECK(source.find("if (previousClip.w <= 0.0)") != std::string::npos);
        CHECK(source.find("return vec2(MV_MAX_NDC);") != std::string::npos);
        CHECK(source.find("previousClip.xy / previousClip.w - currentClip.xy / currentClip.w") != std::string::npos);
        CHECK(source.find("clamp(motion, vec2(-MV_MAX_NDC), vec2(MV_MAX_NDC))") != std::string::npos);
    }
}
