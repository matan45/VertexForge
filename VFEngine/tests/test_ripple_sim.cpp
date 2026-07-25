#include <doctest.h>
#include <water/RippleSimMath.hpp>

#include <cmath>
#include <limits>
#include <vector>

// ==================================================================================
// VK-1606: interactive water ripple simulation
// (CPU twin of resources/shaders/water/ripple_sim.glsl)
// ==================================================================================

namespace
{
    // A square grid with a one-texel zero halo, so interior updates never index out of bounds and
    // the boundary is Dirichlet (h = 0) exactly like the patch border on the GPU.
    struct RippleGrid
    {
        uint32_t n = 0;
        float texelSize = 0.0f;
        std::vector<water::RippleTexel> cells;   // (n + 2) squared, index (y + 1) * stride + (x + 1)

        RippleGrid(uint32_t size, float dx)
            : n(size), texelSize(dx), cells(static_cast<size_t>(size + 2) * (size + 2))
        {
        }

        [[nodiscard]] uint32_t stride() const { return n + 2; }

        water::RippleTexel& at(int x, int y)
        {
            return cells[static_cast<size_t>(y + 1) * stride() + static_cast<size_t>(x + 1)];
        }

        [[nodiscard]] const water::RippleTexel& at(int x, int y) const
        {
            return cells[static_cast<size_t>(y + 1) * stride() + static_cast<size_t>(x + 1)];
        }

        void step(float waveSpeed, float dampingFactor, float dt)
        {
            std::vector<water::RippleTexel> next = cells;
            for (int y = 0; y < static_cast<int>(n); ++y)
            {
                for (int x = 0; x < static_cast<int>(n); ++x)
                {
                    const float lap = water::rippleLaplacian(at(x, y).height,
                                                             at(x - 1, y).height, at(x + 1, y).height,
                                                             at(x, y - 1).height, at(x, y + 1).height,
                                                             texelSize);
                    next[static_cast<size_t>(y + 1) * stride() + static_cast<size_t>(x + 1)] =
                        water::rippleStep(at(x, y), lap, waveSpeed, dampingFactor, dt);
                }
            }
            cells.swap(next);
        }

        [[nodiscard]] float maxAbsHeight() const
        {
            float m = 0.0f;
            for (const auto& c : cells)
            {
                if (!std::isfinite(c.height))
                    return std::numeric_limits<float>::infinity();
                m = std::max(m, std::abs(c.height));
            }
            return m;
        }
    };

    // The checkerboard is the eigenmode with the largest eigenvalue of the 5-point Laplacian
    // (lap = -8h/dx^2), i.e. exactly the mode the CFL bound is derived from. Any weaker excitation
    // would make an "unstable" configuration look stable for far longer than the test runs.
    void seedCheckerboard(RippleGrid& grid, float amplitude = 1.0f)
    {
        for (int y = 0; y < static_cast<int>(grid.n); ++y)
            for (int x = 0; x < static_cast<int>(grid.n); ++x)
                grid.at(x, y).height = ((x + y) % 2 == 0) ? amplitude : -amplitude;
    }
}

TEST_SUITE("RippleSim") {

TEST_CASE("isolated texel reproduces the analytic damped oscillator") {
    // With all four neighbours pinned at zero the Laplacian collapses to -4h/dx^2, so the texel is
    // a single damped harmonic oscillator with omega0 = 2c/dx and gamma = dampingPerSecond / 2.
    // That is the whole point of expressing damping as a per-second rate: it gives the discrete
    // scheme a closed-form reference to be checked against.
    const float dx = 0.2f;
    const float c = 1.0f;
    const float lambda = 1.0f;               // per-second velocity decay
    const float dt = 1.0e-4f;

    const float omega0 = 2.0f * c / dx;      // 10 rad/s
    const float gamma = 0.5f * lambda;
    const float omegaD = std::sqrt(omega0 * omega0 - gamma * gamma);
    const float damping = water::rippleDampingFactor(lambda, dt);

    REQUIRE(water::rippleIsStable(c, dt, dx));

    water::RippleTexel cell;
    cell.height = 1.0f;
    cell.velocity = 0.0f;

    float maxError = 0.0f;
    const int steps = 3000;                  // 0.3 s, ~3 full oscillations
    for (int i = 1; i <= steps; ++i)
    {
        const float lap = water::rippleLaplacian(cell.height, 0.0f, 0.0f, 0.0f, 0.0f, dx);
        cell = water::rippleStep(cell, lap, c, damping, dt);

        const float t = static_cast<float>(i) * dt;
        const float analytic = std::exp(-gamma * t) *
                               (std::cos(omegaD * t) + (gamma / omegaD) * std::sin(omegaD * t));
        maxError = std::max(maxError, std::abs(cell.height - analytic));
    }

    // Symplectic Euler staggers position and velocity by half a step, so the residual is O(dt * v).
    CHECK(maxError < 3.0e-3f);

    // And the envelope really is exp(-gamma t), not merely "something decaying". (The true envelope
    // is exp(-gamma t) * sqrt(1 + (gamma/omegaD)^2), hence the small headroom.)
    CHECK(std::abs(cell.height) < std::exp(-gamma * 0.3f) * 1.05f);
}

TEST_CASE("CFL bound separates stable from divergent") {
    const float dx = 0.2f;
    const float dt = 1.0f / 60.0f;
    const float cMax = water::rippleMaxWaveSpeed(dt, dx);

    // 1/sqrt(2) * dx / dt
    CHECK(cMax == doctest::Approx(water::RIPPLE_CFL_LIMIT * dx / dt));

    SUBCASE("just below the bound stays bounded") {
        const float c = 0.98f * cMax;
        REQUIRE(water::rippleIsStable(c, dt, dx));

        RippleGrid grid(16, dx);
        seedCheckerboard(grid);
        for (int i = 0; i < 3000; ++i)
            grid.step(c, 1.0f, dt);          // damping factor 1 => pure stability test

        CHECK(grid.maxAbsHeight() < 5.0f);
    }

    SUBCASE("just above the bound diverges") {
        const float c = 1.05f * cMax;
        REQUIRE_FALSE(water::rippleIsStable(c, dt, dx));

        RippleGrid grid(16, dx);
        seedCheckerboard(grid);
        for (int i = 0; i < 100; ++i)
            grid.step(c, 1.0f, dt);

        CHECK(grid.maxAbsHeight() > 1.0e6f);
    }
}

TEST_CASE("wave speed clamp can never return a divergent value") {
    const float dx = water::rippleTexelSize(water::RIPPLE_DEFAULT_PATCH_SIZE);
    CHECK(dx == doctest::Approx(100.0f / 512.0f));

    const float dt = water::RIPPLE_SIM_STEP;

    CHECK(water::rippleClampWaveSpeed(1000.0f, dt, dx) == doctest::Approx(water::rippleMaxWaveSpeed(dt, dx)));
    CHECK(water::rippleIsStable(water::rippleClampWaveSpeed(1000.0f, dt, dx), dt, dx));
    CHECK(water::rippleClampWaveSpeed(-5.0f, dt, dx) == 0.0f);
    CHECK(water::rippleClampWaveSpeed(2.0f, dt, dx) == doctest::Approx(2.0f));   // already safe

    // Degenerate inputs must clamp to zero rather than to infinity.
    CHECK(water::rippleClampWaveSpeed(3.0f, 0.0f, dx) == 0.0f);
    CHECK(water::rippleClampWaveSpeed(3.0f, dt, 0.0f) == 0.0f);
}

TEST_CASE("impulse kernel is compact and smooth at the rim") {
    const float r = 2.0f;

    CHECK(water::rippleImpulseKernel(0.0f, r) == doctest::Approx(1.0f));
    CHECK(water::rippleImpulseKernel(r, r) == 0.0f);
    CHECK(water::rippleImpulseKernel(r * 4.0f, r) == 0.0f);
    CHECK(water::rippleImpulseKernel(0.0f, 0.0f) == 0.0f);

    // (1 - t^2)^3 at t = 0.5
    CHECK(water::rippleImpulseKernel(1.0f, r) == doctest::Approx(0.421875f));

    // Monotone decreasing, and the rim approach is flat (zero derivative) rather than a cone tip -
    // a kink there would inject permanent high-frequency ringing the damping cannot remove.
    float prev = water::rippleImpulseKernel(0.0f, r);
    for (int i = 1; i <= 200; ++i)
    {
        const float d = r * static_cast<float>(i) / 200.0f;
        const float k = water::rippleImpulseKernel(d, r);
        CHECK(k <= prev);
        prev = k;
    }
    CHECK(water::rippleImpulseKernel(r * 0.99f, r) < 1.0e-4f);

    SUBCASE("velocity injection is signed and centred on the impulse") {
        water::WaterImpulse imp;
        imp.positionXZ = glm::vec2(10.0f, -4.0f);
        imp.radius = 3.0f;
        imp.strength = -2.5f;

        CHECK(water::rippleImpulseVelocity(imp.positionXZ, imp) == doctest::Approx(-2.5f));
        CHECK(water::rippleImpulseVelocity(imp.positionXZ + glm::vec2(3.0f, 0.0f), imp) == 0.0f);
        CHECK(water::rippleImpulseVelocity(glm::vec2(0.0f), imp) == 0.0f);
    }
}

// VK-1607 review finding #10: changing the patch size at runtime silently re-interpreted the whole
// ping-pong state at a new texel scale and compared origins snapped to two different lattices.
TEST_CASE("a patch-size change invalidates the field") {
    SUBCASE("any real change arms the reset") {
        CHECK(water::rippleNeedsReset(100.0f, 200.0f));
        CHECK(water::rippleNeedsReset(200.0f, 100.0f));
        CHECK(water::rippleNeedsReset(100.0f, 100.01f));
        // The editor slider's whole authored range, against the default.
        CHECK(water::rippleNeedsReset(water::RIPPLE_DEFAULT_PATCH_SIZE, 20.0f));
        CHECK(water::rippleNeedsReset(water::RIPPLE_DEFAULT_PATCH_SIZE, 400.0f));
    }

    SUBCASE("re-publishing the same value does not") {
        // updateWater pushes the params EVERY frame, so a reset on equality would clear the patch
        // continuously and nothing would ever ripple.
        CHECK_FALSE(water::rippleNeedsReset(100.0f, 100.0f));
        CHECK_FALSE(water::rippleNeedsReset(water::RIPPLE_DEFAULT_PATCH_SIZE,
                                            water::RIPPLE_DEFAULT_PATCH_SIZE));
        CHECK_FALSE(water::rippleNeedsReset(100.0f, 100.0f + 1.0e-6f));
    }
}

TEST_CASE("origin snapping holds at non-default patch sizes") {
    // The editor exposes 20..400 m; every other ripple test uses only the 100 m default, so the
    // lattice maths at the ends of that range was untested.
    for (float patch : {20.0f, 37.5f, 400.0f}) {
        const float ts = water::rippleTexelSize(patch);
        REQUIRE(ts > 0.0f);

        const glm::vec2 cam(1234.5f, -987.25f);
        const glm::vec2 origin = water::rippleSnapOrigin(cam, patch);

        CHECK(std::abs(origin.x / ts - std::round(origin.x / ts)) < 1.0e-3f);
        CHECK(std::abs(origin.y / ts - std::round(origin.y / ts)) < 1.0e-3f);

        // Moving by whole texels moves the origin by exactly that, at this patch size too.
        const glm::vec2 moved = water::rippleSnapOrigin(cam + glm::vec2(5.0f * ts, 0.0f), patch);
        CHECK(moved.x - origin.x == doctest::Approx(5.0f * ts));
        CHECK(moved.y == doctest::Approx(origin.y));

        // And the scroll between them is the exact integer shift the re-index depends on.
        const glm::ivec2 scroll = water::rippleScrollOffset(moved, origin, ts);
        CHECK(scroll.x == 5);
        CHECK(scroll.y == 0);
    }
}

TEST_CASE("origin snapping quantises camera motion to whole texels") {
    const float patch = water::RIPPLE_DEFAULT_PATCH_SIZE;
    const float ts = water::rippleTexelSize(patch);

    SUBCASE("the origin always lands on the lattice") {
        // Including at negative coordinates, where a truncating cast (rather than floor) would round
        // toward zero and shift the lattice across the world origin.
        for (float cx : {0.0f, 5.0f, -5.0f, 1234.5f, -9876.25f})
        {
            const glm::vec2 o = water::rippleSnapOrigin(glm::vec2(cx, cx * 0.5f), patch);
            CHECK(std::abs(o.x / ts - std::round(o.x / ts)) < 1.0e-3f);
            CHECK(std::abs(o.y / ts - std::round(o.y / ts)) < 1.0e-3f);
        }
    }

    SUBCASE("moving by whole texels moves the origin by exactly that") {
        const glm::vec2 cam(123.4f, -77.7f);
        const glm::vec2 base = water::rippleSnapOrigin(cam, patch);
        for (int k : {1, 7, -4, -33})
        {
            const glm::vec2 moved = water::rippleSnapOrigin(cam + glm::vec2(static_cast<float>(k) * ts, 0.0f), patch);
            CHECK(moved.x - base.x == doctest::Approx(static_cast<float>(k) * ts));
            CHECK(moved.y == doctest::Approx(base.y));
        }
    }

    SUBCASE("a continuous sweep advances the origin one texel at a time") {
        // This is the anti-crawl property: the origin is a step function of camera position, so the
        // field is never resampled at a sub-texel offset.
        const float startX = 500.0f;
        const int samples = 900;
        const float sweep = 3.0f * ts;

        glm::vec2 prev = water::rippleSnapOrigin(glm::vec2(startX, 0.0f), patch);
        int changes = 0;
        for (int i = 1; i <= samples; ++i)
        {
            const float x = startX + sweep * static_cast<float>(i) / static_cast<float>(samples);
            const glm::vec2 o = water::rippleSnapOrigin(glm::vec2(x, 0.0f), patch);

            CHECK(o.x >= prev.x);                                       // never moves backwards
            if (o.x != prev.x)
            {
                ++changes;
                CHECK(o.x - prev.x == doctest::Approx(ts));              // and always by one texel
            }
            prev = o;
        }
        CHECK(changes == 3);
    }
}

TEST_CASE("scroll re-index maps a texel back to the same world position") {
    const float patch = water::RIPPLE_DEFAULT_PATCH_SIZE;
    const float ts = water::rippleTexelSize(patch);

    const glm::vec2 originPrev = water::rippleSnapOrigin(glm::vec2(0.0f, 0.0f), patch);
    const glm::vec2 originCurr = water::rippleSnapOrigin(glm::vec2(7.3f, -2.9f), patch);
    const glm::ivec2 offset = water::rippleScrollOffset(originCurr, originPrev, ts);

    CHECK(offset.x > 0);
    CHECK(offset.y < 0);

    // THE correctness property: whenever a source texel exists, it describes the same patch of
    // water. If this drifts, the field is being resampled every frame and the surface crawls.
    for (int y : {0, 1, 137, 255, 511})
    {
        for (int x : {0, 1, 137, 255, 511})
        {
            glm::ivec2 src;
            if (!water::rippleSourceIndex(glm::ivec2(x, y), offset, src))
                continue;

            const glm::vec2 dstWorld = water::rippleTexelCenter(glm::ivec2(x, y), originCurr, ts);
            const glm::vec2 srcWorld = water::rippleTexelCenter(src, originPrev, ts);
            CHECK(std::abs(dstWorld.x - srcWorld.x) < 1.0e-3f);
            CHECK(std::abs(dstWorld.y - srcWorld.y) < 1.0e-3f);
        }
    }

    SUBCASE("a texel that has just scrolled in has no source") {
        // Moving +X by `offset.x` texels leaves the last `offset.x` columns uncovered.
        glm::ivec2 src;
        const int n = static_cast<int>(water::RIPPLE_RESOLUTION);
        CHECK(water::rippleSourceIndex(glm::ivec2(0, n / 2), offset, src));
        CHECK_FALSE(water::rippleSourceIndex(glm::ivec2(n - 1, n / 2), offset, src));
    }

    SUBCASE("no movement is the identity") {
        const glm::ivec2 zero = water::rippleScrollOffset(originPrev, originPrev, ts);
        CHECK(zero.x == 0);
        CHECK(zero.y == 0);

        glm::ivec2 src;
        CHECK(water::rippleSourceIndex(glm::ivec2(42, 99), zero, src));
        CHECK(src.x == 42);
        CHECK(src.y == 99);
    }
}

TEST_CASE("scrolling out and back restores the overlap exactly") {
    const uint32_t n = 32;
    std::vector<float> field(static_cast<size_t>(n) * n);
    for (uint32_t i = 0; i < field.size(); ++i)
        field[i] = static_cast<float>(i % 17) - 8.0f;    // deterministic, no repeats within a row
    const std::vector<float> original = field;

    const glm::ivec2 out(5, -3);
    const glm::ivec2 back(-5, 3);

    auto reindex = [&](const std::vector<float>& src, const glm::ivec2& offset)
    {
        std::vector<float> dst(src.size(), 0.0f);
        for (int y = 0; y < static_cast<int>(n); ++y)
        {
            for (int x = 0; x < static_cast<int>(n); ++x)
            {
                glm::ivec2 s;
                if (water::rippleSourceIndex(glm::ivec2(x, y), offset, s, n))
                    dst[static_cast<size_t>(y) * n + static_cast<size_t>(x)] =
                        src[static_cast<size_t>(s.y) * n + static_cast<size_t>(s.x)];
            }
        }
        return dst;
    };

    field = reindex(reindex(field, out), back);

    for (int y = 0; y < static_cast<int>(n); ++y)
    {
        for (int x = 0; x < static_cast<int>(n); ++x)
        {
            // A texel survives the round trip exactly when the SECOND hop found a source: the first
            // hop can always supply it (its own source index is (x, y), which is in range by
            // construction), so `back` is the binding constraint, not `out`.
            glm::ivec2 mid;
            const bool survived = water::rippleSourceIndex(glm::ivec2(x, y), back, mid, n);
            const float got = field[static_cast<size_t>(y) * n + static_cast<size_t>(x)];

            if (survived)
                CHECK(got == original[static_cast<size_t>(y) * n + static_cast<size_t>(x)]);
            else
                CHECK(got == 0.0f);          // scrolled-in area reads zero, never a clamped border
        }
    }
}

TEST_CASE("foam decays from history and is re-established by an impulse") {
    // Pure decay: exp(-2 * 0.5) = exp(-1)
    CHECK(water::rippleFoamStep(1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 0.5f) == doctest::Approx(std::exp(-1.0f)));

    // A fresh impulse overrides the decayed history rather than adding to it.
    CHECK(water::rippleFoamStep(0.0f, 0.0f, 0.8f, 1.0f, 2.0f, 0.5f) == doctest::Approx(0.8f));

    // Curvature generates foam, and everything stays inside [0, 1].
    CHECK(water::rippleFoamStep(0.0f, 1000.0f, 0.0f, 1.0f, 1.0f, 0.016f) == doctest::Approx(1.0f));
    CHECK(water::rippleFoamStep(1.0f, 1000.0f, 5.0f, 10.0f, 0.0f, 0.016f) == doctest::Approx(1.0f));
    CHECK(water::rippleFoamStep(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.016f) == 0.0f);

    // Foam is curvature-driven, so it does not care about the sign of the disturbance.
    CHECK(water::rippleFoamStep(0.0f, -3.0f, 0.0f, 0.1f, 1.0f, 0.016f) ==
          doctest::Approx(water::rippleFoamStep(0.0f, 3.0f, 0.0f, 0.1f, 1.0f, 0.016f)));
}

TEST_CASE("substep accumulator does not lose time between frames") {
    SUBCASE("61 fps still simulates ~60 steps per second") {
        float acc = 0.0f;
        uint32_t total = 0;
        const float frameDt = 1.0f / 61.0f;
        for (int i = 0; i < 610; ++i)
            total += water::rippleSubstepCount(acc, frameDt);

        // 10 s of wall clock at a fixed 1/60 s step. A drop-remainder implementation returns 0 here.
        CHECK(total >= 597);
        CHECK(total <= 601);
    }

    SUBCASE("240 fps runs a step only every fourth frame") {
        float acc = 0.0f;
        uint32_t total = 0;
        for (int i = 0; i < 240; ++i)
            total += water::rippleSubstepCount(acc, 1.0f / 240.0f);

        CHECK(total >= 59);
        CHECK(total <= 61);
    }

    SUBCASE("a long hitch is capped and the backlog is abandoned") {
        float acc = 0.0f;
        CHECK(water::rippleSubstepCount(acc, 5.0f) == water::RIPPLE_MAX_SUBSTEPS);

        // Sustained overload must not let the accumulator grow without bound.
        for (int i = 0; i < 100; ++i)
        {
            CHECK(water::rippleSubstepCount(acc, 1.0f) <= water::RIPPLE_MAX_SUBSTEPS);
            CHECK(acc <= water::RIPPLE_SIM_STEP * static_cast<float>(water::RIPPLE_MAX_SUBSTEPS));
        }
    }

    SUBCASE("a zero-length frame runs nothing") {
        float acc = 0.0f;
        CHECK(water::rippleSubstepCount(acc, 0.0f) == 0);
        CHECK(water::rippleSubstepCount(acc, -1.0f) == 0);
    }
}

}   // TEST_SUITE
