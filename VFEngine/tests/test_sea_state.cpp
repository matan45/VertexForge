#include <doctest.h>
#include <water/SeaState.hpp>
#include <weather/WeatherTypes.hpp>

// ============================================================
// Weather-driven sea state mapping (water epic Phase 2)
// ============================================================

TEST_SUITE("SeaState") {

// ---- Beaufort <-> wind speed ----

TEST_CASE("windSpeedFromBeaufort and beaufortFromWindSpeed are inverses") {
    for (float bf = 0.0f; bf <= 12.0f; bf += 1.5f) {
        float ms = water::windSpeedFromBeaufort(bf);
        CHECK(water::beaufortFromWindSpeed(ms) == doctest::Approx(bf).epsilon(0.001));
    }
}

TEST_CASE("Beaufort thresholds roughly match the standard scale") {
    // B6 = strong breeze, ~10.8-13.8 m/s
    float b6 = water::windSpeedFromBeaufort(6.0f);
    CHECK(b6 > 10.0f);
    CHECK(b6 < 14.0f);

    // B12 = hurricane, >= 32.6 m/s
    CHECK(water::windSpeedFromBeaufort(12.0f) > 32.0f);

    CHECK(water::windSpeedFromBeaufort(0.0f) == doctest::Approx(0.0f));
}

// ---- seaStateFromBeaufort ----

TEST_CASE("Anchor: Beaufort 6 reproduces OceanComponent band defaults") {
    auto state = water::seaStateFromBeaufort(6.0f, 45.0f);

    // Swell band defaults: 12 m/s wind, 0.00005 amplitude, 1.5 choppiness
    CHECK(state.bands[0].windSpeed == doctest::Approx(12.0f).epsilon(0.05));
    CHECK(state.bands[0].amplitude == doctest::Approx(0.00005f));
    CHECK(state.bands[0].choppiness == doctest::Approx(1.5f));
    CHECK(state.bands[0].windDirection == doctest::Approx(45.0f));
}

TEST_CASE("Amplitude and wind speed increase monotonically with Beaufort") {
    float prevAmplitude = -1.0f;
    float prevWind = -1.0f;
    for (float bf = 1.0f; bf <= 12.0f; bf += 0.5f) {
        auto state = water::seaStateFromBeaufort(bf, 0.0f);
        CHECK(state.bands[0].amplitude > prevAmplitude);
        CHECK(state.bands[0].windSpeed > prevWind);
        prevAmplitude = state.bands[0].amplitude;
        prevWind = state.bands[0].windSpeed;
    }
}

TEST_CASE("Storms foam earlier than calm seas") {
    auto calm = water::seaStateFromBeaufort(1.0f, 0.0f);
    auto storm = water::seaStateFromBeaufort(12.0f, 0.0f);
    CHECK(storm.bands[0].foamThreshold > calm.bands[0].foamThreshold);
}

TEST_CASE("Beaufort is clamped to [0, 12]") {
    auto over = water::seaStateFromBeaufort(99.0f, 0.0f);
    auto max = water::seaStateFromBeaufort(12.0f, 0.0f);
    CHECK(over.bands[0].amplitude == doctest::Approx(max.bands[0].amplitude));

    auto under = water::seaStateFromBeaufort(-5.0f, 0.0f);
    auto zero = water::seaStateFromBeaufort(0.0f, 0.0f);
    CHECK(under.bands[0].windSpeed == doctest::Approx(zero.bands[0].windSpeed));
}

TEST_CASE("Presets map to ascending Beaufort numbers") {
    CHECK(water::beaufortForPreset(water::SeaStatePreset::Calm)
          < water::beaufortForPreset(water::SeaStatePreset::Slight));
    CHECK(water::beaufortForPreset(water::SeaStatePreset::Rough)
          < water::beaufortForPreset(water::SeaStatePreset::Storm));
    CHECK(water::beaufortForPreset(water::SeaStatePreset::Storm) == doctest::Approx(12.0f));
}

// ---- lerpSeaState ----

TEST_CASE("lerpSeaState endpoints") {
    auto a = water::seaStateFromBeaufort(2.0f, 10.0f);
    auto b = water::seaStateFromBeaufort(10.0f, 200.0f);

    auto at0 = water::lerpSeaState(a, b, 0.0f);
    auto at1 = water::lerpSeaState(a, b, 1.0f);
    CHECK(at0.bands[0].amplitude == doctest::Approx(a.bands[0].amplitude));
    CHECK(at1.bands[0].amplitude == doctest::Approx(b.bands[0].amplitude));
}

TEST_CASE("Wind direction lerps across the 0/360 wrap, not the long way") {
    // 350 -> 10 should pass through 0 (i.e. 355 at t=0.25), not through 180
    CHECK(water::lerpAngleDeg(350.0f, 10.0f, 0.5f) == doctest::Approx(360.0f));
    CHECK(water::lerpAngleDeg(350.0f, 10.0f, 0.25f) == doctest::Approx(355.0f));
    CHECK(water::lerpAngleDeg(10.0f, 350.0f, 0.5f) == doctest::Approx(0.0f));
}

// ---- weather mapping + quantization ----

TEST_CASE("mapWeatherToSeaState follows weather wind") {
    weather::WeatherState calm;
    calm.windSpeed = 1.0f;
    calm.gustStrength = 0.0f;

    weather::WeatherState storm;
    storm.windSpeed = 30.0f;
    storm.gustStrength = 1.0f;

    auto calmSea = water::mapWeatherToSeaState(calm, 1.0f);
    auto stormSea = water::mapWeatherToSeaState(storm, 1.0f);
    CHECK(stormSea.bands[0].amplitude > calmSea.bands[0].amplitude);
    CHECK(stormSea.bands[0].windSpeed > calmSea.bands[0].windSpeed);
}

TEST_CASE("Response scales how strongly the ocean follows weather") {
    weather::WeatherState ws;
    ws.windSpeed = 20.0f;
    ws.gustStrength = 0.0f;

    CHECK(water::beaufortFromWeather(ws, 0.5f) ==
          doctest::Approx(water::beaufortFromWindSpeed(20.0f) * 0.5f));
    CHECK(water::beaufortFromWeather(ws, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("Quantization suppresses tiny per-frame deltas") {
    // Two nearly identical weather samples (a slow transition one frame apart)
    // must quantize to the same value, so no ocean config version churn
    float a = water::quantizeBeaufort(4.213f);
    float b = water::quantizeBeaufort(4.219f);
    CHECK(a == b);

    float dirA = water::quantizeDirectionDeg(101.2f);
    float dirB = water::quantizeDirectionDeg(101.9f);
    CHECK(dirA == dirB);

    // But a real change still gets through
    CHECK(water::quantizeBeaufort(4.2f) != water::quantizeBeaufort(4.5f));
}

}
