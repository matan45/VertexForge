#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

// Shared CPU-side mirror of the billboard/VFX flipbook + animation math.
//
// This header is the single source of truth for the flipbook UV transform so
// that the CPU gather path (BillboardBufferManager) and unit tests compute the
// exact same values that the GLSL vertex shader does. Keep this in lockstep
// with resources/shaders/billboard/billboard.glsl and
// resources/shaders/vfx/vfx_billboard_gpu.glsl.
namespace render
{
    struct FlipbookFrame
    {
        glm::vec2 uvOffset; // tile origin within the [0,1] atlas
        glm::vec2 uvScale;  // size of a single tile within the [0,1] atlas
    };

    // Current + next flipbook cell plus the fractional blend factor, for frame
    // crossfading (VK-1469). uvScale is the single-tile size (same for both cells).
    struct FlipbookBlendFrame
    {
        glm::vec2 uvOffsetCurr; // current tile origin within the [0,1] atlas
        glm::vec2 uvOffsetNext; // next tile origin (wrapped or clamped)
        glm::vec2 uvScale;      // size of a single tile within the [0,1] atlas
        float uvBlend;          // fractional frame position in [0,1)
    };

    // Returns the sub-rect (offset + scale) for the current flipbook frame.
    // Mirrors the VFX/billboard flipbook math: totalFrames = cols*rows, frame
    // advances at frameRate frames/sec. When loop==true the frame wraps via mod();
    // when loop==false the frame is clamped to the last tile so a one-shot
    // animation HOLDS its final frame after one cycle. When there is no animation
    // (<=1 frame or non-positive frame rate) the full [0,1] rect is returned so
    // sampling is identical to a non-animated billboard.
    inline FlipbookFrame computeFlipbookFrame(float time, float frameRate, int cols, int rows, bool loop)
    {
        const int totalFrames = cols * rows;
        if (totalFrames <= 1 || frameRate <= 0.0f)
        {
            return {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f)};
        }

        const float total = static_cast<float>(totalFrames);
        float floored;
        if (loop)
        {
            float frame = std::fmod(time * frameRate, total);
            if (frame < 0.0f)
            {
                frame += total;
            }
            floored = std::floor(frame);
        }
        else
        {
            // Play-once: advance once, then hold the last tile (total-1).
            floored = std::min(std::floor(time * frameRate), total - 1.0f);
            if (floored < 0.0f)
            {
                floored = 0.0f;
            }
        }

        const float colsF = static_cast<float>(cols);
        const float col = std::fmod(floored, colsF);
        const float row = std::floor(floored / colsF);

        const glm::vec2 tileSize(1.0f / colsF, 1.0f / static_cast<float>(rows));
        return {glm::vec2(col, row) * tileSize, tileSize};
    }

    // Backward-compatible 4-arg form: always loops (the original behavior).
    inline FlipbookFrame computeFlipbookFrame(float time, float frameRate, int cols, int rows)
    {
        return computeFlipbookFrame(time, frameRate, cols, rows, true);
    }

    // Crossfade variant of the flipbook transform used by VFX frame blending
    // (VK-1469). frameIndex is the post-wrap frame position in [0, cols*rows) —
    // exactly what the VFX billboard shaders hold at their blend point. Returns the
    // tile origins for the current AND next cell plus the fractional blend factor,
    // so the caller can mix() between the two cells. loop==true wraps the next cell
    // (last->first); loop==false clamps it to the last tile so a one-shot/play-once
    // flipbook HOLDS its final frame. When there is a single frame (<=1) the full
    // [0,1] rect is returned with blend 0 (no crossfade). Keep in lockstep with the
    // blend blocks in resources/shaders/vfx/vfx_billboard_gpu.glsl and
    // resources/shaders/vfx/vfx_billboard.glsl.
    inline FlipbookBlendFrame computeFlipbookBlendFrame(float frameIndex, int cols, int rows, bool loop)
    {
        const int totalFrames = cols * rows;
        if (totalFrames <= 1)
        {
            return {glm::vec2(0.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f};
        }

        const float total = static_cast<float>(totalFrames);
        const float colsF = static_cast<float>(cols);
        const float curr = std::floor(frameIndex);
        const float blend = frameIndex - curr;
        const float next = loop ? std::fmod(curr + 1.0f, total)
                                : std::min(curr + 1.0f, total - 1.0f);

        const glm::vec2 tileSize(1.0f / colsF, 1.0f / static_cast<float>(rows));
        const glm::vec2 offCurr(std::fmod(curr, colsF), std::floor(curr / colsF));
        const glm::vec2 offNext(std::fmod(next, colsF), std::floor(next / colsF));
        return {offCurr * tileSize, offNext * tileSize, tileSize, blend};
    }

    // True only when a one-shot (loop==false) flipbook has reached the end of its
    // single cycle, i.e. time*frameRate >= totalFrames. Returns false for looping
    // or non-animated billboards (which never "finish").
    inline bool flipbookFinished(float time, float frameRate, int cols, int rows, bool loop)
    {
        if (loop)
        {
            return false;
        }
        const int totalFrames = cols * rows;
        if (totalFrames <= 1 || frameRate <= 0.0f)
        {
            return false;
        }
        return time * frameRate >= static_cast<float>(totalFrames);
    }

    // Pulse (scale throb) multiplier applied to billboard size.
    // Returns 1.0 when amplitude is zero (no animation).
    inline float pulseScale(float t, float startTime, float amp, float freq)
    {
        return 1.0f + amp * std::sin(freq * (t - startTime));
    }

    // Spin angle (radians) about the billboard's view-plane normal.
    // Returns 0.0 when speed is zero (no animation).
    inline float spinAngle(float t, float startTime, float speed)
    {
        return speed * (t - startTime);
    }
}
