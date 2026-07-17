#include <doctest.h>
#include <types/RenderSettings.hpp>
#include <types/RenderSettingsConfig.hpp>

// ============================================================
// VK-1534: runtime scalability surface — pure config/helper tests
// (int<->enum clamping, config-driven RenderSettings assembly, HUD math)
// ============================================================

TEST_SUITE("GfxSettingsConfig") {

// ---- clampPreset ----

TEST_CASE("clampPreset: in-range values map identically") {
    CHECK(types::clampPreset(0) == types::RenderPreset::Low);
    CHECK(types::clampPreset(1) == types::RenderPreset::Medium);
    CHECK(types::clampPreset(2) == types::RenderPreset::High);
    CHECK(types::clampPreset(3) == types::RenderPreset::Ultra);
    CHECK(types::clampPreset(4) == types::RenderPreset::Custom);
}

TEST_CASE("clampPreset: out-of-range falls back to High") {
    CHECK(types::clampPreset(-1) == types::RenderPreset::High);
    CHECK(types::clampPreset(5) == types::RenderPreset::High);
    CHECK(types::clampPreset(9999) == types::RenderPreset::High);
}

// ---- clampPresentMode ----

TEST_CASE("clampPresentMode: in-range values map identically") {
    CHECK(types::clampPresentMode(0) == types::PresentMode::Fifo);
    CHECK(types::clampPresentMode(1) == types::PresentMode::Mailbox);
    CHECK(types::clampPresentMode(2) == types::PresentMode::Immediate);
}

TEST_CASE("clampPresentMode: out-of-range falls back to Mailbox") {
    CHECK(types::clampPresentMode(-1) == types::PresentMode::Mailbox);
    CHECK(types::clampPresentMode(3) == types::PresentMode::Mailbox);
}

// ---- clampMsaa ----

TEST_CASE("clampMsaa: in-range values map identically") {
    CHECK(types::clampMsaa(0) == types::MsaaSamples::Off);
    CHECK(types::clampMsaa(1) == types::MsaaSamples::X2);
    CHECK(types::clampMsaa(2) == types::MsaaSamples::X4);
    CHECK(types::clampMsaa(3) == types::MsaaSamples::X8);
}

TEST_CASE("clampMsaa: out-of-range falls back to Off") {
    CHECK(types::clampMsaa(-1) == types::MsaaSamples::Off);
    CHECK(types::clampMsaa(4) == types::MsaaSamples::Off);
}

// ---- buildFromConfig ----

TEST_CASE("buildFromConfig: sets activePreset and overrides display") {
    auto s = types::buildFromConfig(0 /*Low*/, 0 /*Fifo*/, 2 /*X4*/);
    CHECK(s.activePreset == types::RenderPreset::Low);
    CHECK(s.display.presentMode == types::PresentMode::Fifo);
    CHECK(s.display.msaa == types::MsaaSamples::X4);
}

TEST_CASE("buildFromConfig: preset drives the scalability sub-structs (matches fromPreset)") {
    auto s = types::buildFromConfig(0 /*Low*/, 1, 0);
    auto ref = types::RenderSettings::fromPreset(types::RenderPreset::Low);
    // Display was overridden, but the preset-derived fields must equal fromPreset(Low).
    CHECK(s.shadows.quality == ref.shadows.quality);
    CHECK(s.culling.globalLodBias == doctest::Approx(ref.culling.globalLodBias));
    CHECK(s.distanceCulling.enabled == ref.distanceCulling.enabled);
    CHECK(s.vfxLOD.lod0Distance == doctest::Approx(ref.vfxLOD.lod0Distance));

    auto ultra = types::buildFromConfig(3 /*Ultra*/, 1, 0);
    CHECK(ultra.shadows.quality == types::ShadowQuality::Ultra);
    CHECK(ultra.shadows.quality != s.shadows.quality); // preset actually changed things
}

TEST_CASE("buildFromConfig: out-of-range inputs are clamped, never invalid enums") {
    auto s = types::buildFromConfig(99, -5, 42);
    CHECK(s.activePreset == types::RenderPreset::High);      // clamped
    CHECK(s.display.presentMode == types::PresentMode::Mailbox); // clamped
    CHECK(s.display.msaa == types::MsaaSamples::Off);        // clamped
}

// ---- emaFps ----

TEST_CASE("emaFps: seeds with the instantaneous value when prevEma <= 0") {
    // 60 FPS frame time: instant = 60.
    CHECK(types::emaFps(0.0, 1.0 / 60.0, 0.1) == doctest::Approx(60.0));
    CHECK(types::emaFps(-1.0, 1.0 / 30.0, 0.1) == doctest::Approx(30.0));
}

TEST_CASE("emaFps: a zero/negative dt carries no new sample") {
    CHECK(types::emaFps(60.0, 0.0, 0.1) == doctest::Approx(60.0));
    CHECK(types::emaFps(60.0, -0.5, 0.1) == doctest::Approx(60.0));
}

TEST_CASE("emaFps: one step blends toward the instantaneous value") {
    // prev 60, this frame 30 FPS (dt=1/30), alpha 0.1 -> 60 + 0.1*(30-60) = 57.
    CHECK(types::emaFps(60.0, 1.0 / 30.0, 0.1) == doctest::Approx(57.0));
}

TEST_CASE("emaFps: converges to the steady-state frame rate") {
    double ema = 0.0;
    const double dt = 1.0 / 50.0; // steady 50 FPS
    for (int i = 0; i < 500; ++i) {
        ema = types::emaFps(ema, dt, 0.1);
    }
    CHECK(ema == doctest::Approx(50.0).epsilon(0.001));
}

// ---- formatHudLine ----

TEST_CASE("formatHudLine: deterministic layout") {
    CHECK(types::formatHudLine(60.0, 16.5, 12.0, 842)
          == "FPS 60  CPU 16.5ms  GPU 12.0ms  Draws 842");
}

TEST_CASE("formatHudLine: FPS is rounded to a whole number") {
    CHECK(types::formatHudLine(59.6, 16.5, 12.0, 100)
          == "FPS 60  CPU 16.5ms  GPU 12.0ms  Draws 100");
}

} // TEST_SUITE
