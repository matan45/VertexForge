#include <doctest.h>
#include <vfx/VFXCurveTypes.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1089: VFX unit tests
// ============================================================

TEST_SUITE("VFX") {

// ---- VFXCurve ----

TEST_CASE("VFXCurve: constant curve evaluates to constant") {
    auto curve = vfx::VFXCurve::constant(5.0f);
    CHECK(curve.evaluate(0.0f) == doctest::Approx(5.0f));
    CHECK(curve.evaluate(0.5f) == doctest::Approx(5.0f));
    CHECK(curve.evaluate(1.0f) == doctest::Approx(5.0f));
}

TEST_CASE("VFXCurve: fromStartEnd at boundaries") {
    auto curve = vfx::VFXCurve::fromStartEnd(0.0f, 10.0f);
    CHECK(curve.evaluate(0.0f) == doctest::Approx(0.0f));
    CHECK(curve.evaluate(1.0f) == doctest::Approx(10.0f));
}

TEST_CASE("VFXCurve: fromStartEnd at midpoint") {
    auto curve = vfx::VFXCurve::fromStartEnd(0.0f, 10.0f);
    float mid = curve.evaluate(0.5f);
    CHECK(mid == doctest::Approx(5.0f).epsilon(0.1f));
}

TEST_CASE("VFXCurve: empty curve") {
    vfx::VFXCurve curve;
    CHECK(curve.empty());
}

TEST_CASE("VFXCurve: non-empty after creation") {
    auto curve = vfx::VFXCurve::constant(1.0f);
    CHECK_FALSE(curve.empty());
}

// ---- VFXGradient ----

TEST_CASE("VFXGradient: fromStartEnd at t=0 returns start color") {
    glm::vec4 start{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 end{0.0f, 0.0f, 1.0f, 1.0f};
    auto gradient = vfx::VFXGradient::fromStartEnd(start, end);
    auto color = gradient.evaluate(0.0f);
    CHECK(color.r == doctest::Approx(1.0f));
    CHECK(color.g == doctest::Approx(0.0f));
    CHECK(color.b == doctest::Approx(0.0f));
    CHECK(color.a == doctest::Approx(1.0f));
}

TEST_CASE("VFXGradient: fromStartEnd at t=1 returns end color") {
    glm::vec4 start{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 end{0.0f, 0.0f, 1.0f, 1.0f};
    auto gradient = vfx::VFXGradient::fromStartEnd(start, end);
    auto color = gradient.evaluate(1.0f);
    CHECK(color.r == doctest::Approx(0.0f));
    CHECK(color.g == doctest::Approx(0.0f));
    CHECK(color.b == doctest::Approx(1.0f));
    CHECK(color.a == doctest::Approx(1.0f));
}

TEST_CASE("VFXGradient: interpolation at midpoint") {
    glm::vec4 start{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 end{1.0f, 1.0f, 1.0f, 1.0f};
    auto gradient = vfx::VFXGradient::fromStartEnd(start, end);
    auto color = gradient.evaluate(0.5f);
    CHECK(color.r == doctest::Approx(0.5f).epsilon(0.05f));
    CHECK(color.a == doctest::Approx(0.5f).epsilon(0.05f));
}

TEST_CASE("VFXGradient: empty gradient") {
    vfx::VFXGradient gradient;
    CHECK(gradient.empty());
}

} // TEST_SUITE
