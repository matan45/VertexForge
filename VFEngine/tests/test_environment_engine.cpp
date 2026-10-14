// VK-1654 — engine-side pieces behind the MCP environment tools: the shared day-night angle
// formula, the directional-light Euler <-> sun-angle inverse, the GI rebuild policy, weather-off
// restoring the authored fog/clouds, and GI following the scene on load.
#include "doctest.h"

#include <glm/glm.hpp>

#include <cmath>

#include <atmosphere/AtmosphereSettings.hpp>
#include <atmosphere/DayNightCycleController.hpp>
#include <atmosphere/SunEntitySync.hpp>
#include <render/gi/GIRebuildPolicy.hpp>
#include <weather/WeatherPresets.hpp>

#include "events/EventDispatcher.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "impl/render/GIServiceImpl.hpp"
#include "impl/weather/WeatherServiceImpl.hpp"
#include "providers/render/IGIProvider.hpp"

#include <vector>

using render::atmosphere::AtmosphereSettings;
using render::atmosphere::DayNightAngles;
using render::atmosphere::DayNightCycleController;
using render::atmosphere::dayNightAngles;
using render::atmosphere::directionalLightEulerForSun;
using render::atmosphere::sunAnglesFromDirectionalLightEuler;
using render::gi::GISettings;
using render::gi::giSettingsRequireRebuild;

namespace
{
    // Smallest difference between two angles in degrees, across the 0/360 wrap.
    float angleDistance(float a, float b)
    {
        const float d = std::fmod(std::abs(a - b), 360.0f);
        return d > 180.0f ? 360.0f - d : d;
    }

    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    // The live renderer copies weather writes into, plus the scene's authored settings.
    struct FakeEnvironment
    {
        types::RenderSettings authored;
        postprocess::PostProcessSettings livePostProcess;
        render::cloud::CloudSettings liveCloud;
        AtmosphereSettings liveAtmosphere;
        int sunManagingAtmosphereApplies = 0; // the plain apply, which may create/delete the editor's auto-Sun

        void registerLiveHandlers()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.registerQueryHandler<events::postprocess::GetPostProcessSettingsQuery>(
                [this](const events::postprocess::GetPostProcessSettingsQuery&) { return livePostProcess; });
            dispatcher.registerCommandHandler<events::postprocess::ApplyPostProcessSettingsCommand>(
                [this](const events::postprocess::ApplyPostProcessSettingsCommand& cmd) { livePostProcess = cmd.settings; });
            dispatcher.registerQueryHandler<events::cloud::GetCloudSettingsQuery>(
                [this](const events::cloud::GetCloudSettingsQuery&) { return liveCloud; });
            dispatcher.registerCommandHandler<events::cloud::ApplyCloudSettingsCommand>(
                [this](const events::cloud::ApplyCloudSettingsCommand& cmd) { liveCloud = cmd.settings; });
            dispatcher.registerQueryHandler<events::atmosphere::GetAtmosphereSettingsQuery>(
                [this](const events::atmosphere::GetAtmosphereSettingsQuery&) { return liveAtmosphere; });
            dispatcher.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsCommand>(
                [this](const events::atmosphere::ApplyAtmosphereSettingsCommand& cmd)
                {
                    ++sunManagingAtmosphereApplies;
                    liveAtmosphere = cmd.settings;
                });
            dispatcher.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsNoSunCommand>(
                [this](const events::atmosphere::ApplyAtmosphereSettingsNoSunCommand& cmd) { liveAtmosphere = cmd.settings; });
        }

        void registerAuthoredHandler()
        {
            events::EventDispatcher::instance().registerQueryHandler<events::scene::GetRenderSettingsQuery>(
                [this](const events::scene::GetRenderSettingsQuery&) { return authored; });
        }
    };

    // Records what GIServiceImpl pushes to the renderer.
    class FakeGIProvider : public services::IGIProvider
    {
    public:
        render::gi::GISettings live;
        std::vector<render::gi::GISettings> applied;

        void applyGISettings(const render::gi::GISettings& settings) override
        {
            applied.push_back(settings);
            live = settings;
        }
        render::gi::GISettings getGISettings() const override { return live; }
        void setGIEnabled(bool enabled) override { live.enabled = enabled; }
        bool isGIEnabled() const override { return live.enabled; }
        render::gi::GIDebugStats getGIDebugStats() const override { return {}; }
        void setShowProbes(bool show) override { live.showProbes = show; }
        void setShowCascadeBounds(bool show) override { live.showCascadeBounds = show; }
        void setShowProbeValidity(bool show) override { live.showProbeValidity = show; }
    };
}

TEST_CASE("environment engine: day-night angles follow the documented sun path")
{
    // 0:00 = nadir, 6:00 = sunrise, 12:00 = zenith, 18:00 = sunset.
    const DayNightAngles midnight = dayNightAngles(0.0f, 0.0f);
    CHECK(midnight.sunElevation == doctest::Approx(-90.0f));
    CHECK(midnight.sunAzimuth == doctest::Approx(90.0f));

    const DayNightAngles sunrise = dayNightAngles(6.0f, 0.0f);
    CHECK(sunrise.sunElevation == doctest::Approx(0.0f));
    CHECK(sunrise.sunAzimuth == doctest::Approx(180.0f));

    const DayNightAngles noon = dayNightAngles(12.0f, 0.0f);
    CHECK(noon.sunElevation == doctest::Approx(90.0f));
    CHECK(noon.sunAzimuth == doctest::Approx(270.0f));

    const DayNightAngles sunset = dayNightAngles(18.0f, 0.0f);
    CHECK(std::abs(sunset.sunElevation) < 1e-3f);
    CHECK(angleDistance(sunset.sunAzimuth, 0.0f) < 1e-3f);

    // Just before sunset: the sun sits a few degrees above the horizon.
    const DayNightAngles evening = dayNightAngles(17.85f, 0.0f);
    CHECK(evening.sunElevation == doctest::Approx(3.5334f).epsilon(0.001));
    CHECK(evening.sunAzimuth == doctest::Approx(357.75f).epsilon(0.0001));
}

TEST_CASE("environment engine: the moon is opposite the sun, shifted by the phase offset")
{
    const DayNightAngles noon = dayNightAngles(12.0f, 0.0f);
    CHECK(noon.moonElevation == doctest::Approx(-noon.sunElevation));
    CHECK(noon.moonAzimuth == doctest::Approx(90.0f)); // (270 + 180) mod 360

    const DayNightAngles quarter = dayNightAngles(12.0f, 0.25f);
    CHECK(quarter.moonElevation == doctest::Approx(-90.0f));
    CHECK(quarter.moonAzimuth == doctest::Approx(180.0f)); // (270 + 180 + 90) mod 360
}

TEST_CASE("environment engine: DayNightCycleController::tick writes the shared angles")
{
    AtmosphereSettings settings;
    settings.dayNightEnabled = true;
    settings.timeOfDay = 17.85f;
    settings.moonPhaseOffset = 0.25f;

    DayNightCycleController controller;
    controller.tick(0.0f, settings); // no time advance: angles only

    const DayNightAngles expected = dayNightAngles(17.85f, 0.25f);
    CHECK(settings.timeOfDay == doctest::Approx(17.85f));
    CHECK(settings.sunAzimuth == doctest::Approx(expected.sunAzimuth));
    CHECK(settings.sunElevation == doctest::Approx(expected.sunElevation));
    CHECK(settings.moonAzimuth == doctest::Approx(expected.moonAzimuth));
    CHECK(settings.moonElevation == doctest::Approx(expected.moonElevation));

    // A disabled cycle leaves everything untouched.
    AtmosphereSettings frozen;
    frozen.dayNightEnabled = false;
    frozen.timeOfDay = 17.85f;
    const float azimuthBefore = frozen.sunAzimuth;
    const float elevationBefore = frozen.sunElevation;
    controller.tick(1.0f, frozen);
    CHECK(frozen.timeOfDay == 17.85f);
    CHECK(frozen.sunAzimuth == azimuthBefore);
    CHECK(frozen.sunElevation == elevationBefore);
}

TEST_CASE("environment engine: sun angles round-trip through the directional-light Euler")
{
    for (int el = -85; el <= 85; el += 5)
    {
        for (int az = 0; az < 360; az += 7)
        {
            const glm::vec3 euler = directionalLightEulerForSun(static_cast<float>(az), static_cast<float>(el));
            const glm::vec2 angles = sunAnglesFromDirectionalLightEuler(euler);

            INFO("azimuth " << az << ", elevation " << el);
            CHECK(angleDistance(angles.x, static_cast<float>(az)) < 1e-3f);
            CHECK(std::abs(angles.y - static_cast<float>(el)) < 1e-3f);
            CHECK(angles.x >= 0.0f);
            CHECK(angles.x < 360.0f);
        }
    }
}

TEST_CASE("environment engine: roll does not change the sun angles of a directional light")
{
    const glm::vec3 euler = directionalLightEulerForSun(123.0f, 20.0f);
    const glm::vec2 plain = sunAnglesFromDirectionalLightEuler(euler);
    const glm::vec2 rolled = sunAnglesFromDirectionalLightEuler(glm::vec3(euler.x, euler.y, 47.0f));
    CHECK(rolled.x == plain.x);
    CHECK(rolled.y == plain.y);
}

TEST_CASE("environment engine: GI rebuilds exactly on probe-grid changes")
{
    const GISettings base = GISettings::fromQuality(render::gi::GIQuality::High);
    CHECK_FALSE(giSettingsRequireRebuild(base, base));

    SUBCASE("structural fields require the full rebuild")
    {
        GISettings next = base;
        SUBCASE("quality") { next.quality = render::gi::GIQuality::Ultra; }
        SUBCASE("enabled") { next.enabled = !base.enabled; }
        SUBCASE("probeSpacing") { next.probeSpacing = base.probeSpacing + 1.0f; }
        SUBCASE("cascadeMultiplier") { next.cascadeMultiplier = base.cascadeMultiplier + 0.5f; }
        SUBCASE("farFieldEnabled") { next.farFieldEnabled = !base.farFieldEnabled; }
        SUBCASE("farFieldCascadeCount") { next.farFieldCascadeCount = base.farFieldCascadeCount + 1; }
        SUBCASE("farFieldProbeSpacing") { next.farFieldProbeSpacing = base.farFieldProbeSpacing * 2.0f; }
        CHECK(giSettingsRequireRebuild(base, next));
    }

    SUBCASE("per-update tuning and debug views apply in place")
    {
        GISettings next = base;
        SUBCASE("probeRaysPerUpdate") { next.probeRaysPerUpdate = base.probeRaysPerUpdate * 2; }
        SUBCASE("temporalBlendFactor") { next.temporalBlendFactor = 0.8f; }
        SUBCASE("probeUpdateRate") { next.probeUpdateRate = 0.5f; }
        SUBCASE("maxProbeDistance") { next.maxProbeDistance = 300.0f; }
        SUBCASE("farFieldMaxDistance") { next.farFieldMaxDistance = 900.0f; }
        SUBCASE("farFieldRaysPerUpdate") { next.farFieldRaysPerUpdate = 64; }
        SUBCASE("farFieldUpdateRate") { next.farFieldUpdateRate = 0.2f; }
        SUBCASE("ssgiEnabled") { next.ssgiEnabled = !base.ssgiEnabled; }
        SUBCASE("ssgiIntensity") { next.ssgiIntensity = 1.5f; }
        SUBCASE("ssgiSampleCount") { next.ssgiSampleCount = 12; }
        SUBCASE("showProbes") { next.showProbes = true; }
        SUBCASE("showCascadeBounds") { next.showCascadeBounds = true; }
        SUBCASE("showProbeValidity") { next.showProbeValidity = true; }
        CHECK_FALSE(giSettingsRequireRebuild(base, next));
    }

    SUBCASE("turning GI on or off rebuilds")
    {
        const GISettings off = GISettings::fromQuality(render::gi::GIQuality::Off);
        CHECK(giSettingsRequireRebuild(off, base));
        CHECK(giSettingsRequireRebuild(base, off));

        GISettings disabled = base;
        disabled.enabled = false;
        CHECK(giSettingsRequireRebuild(base, disabled));
        CHECK(giSettingsRequireRebuild(disabled, base));
    }

    SUBCASE("changes between two inactive states rebuild nothing")
    {
        GISettings disabled = base;
        disabled.enabled = false;

        GISettings next = disabled;
        SUBCASE("quality") { next.quality = render::gi::GIQuality::Ultra; }
        SUBCASE("probeSpacing") { next.probeSpacing = 4.0f; }
        SUBCASE("cascadeMultiplier") { next.cascadeMultiplier = 3.0f; }
        SUBCASE("farFieldCascadeCount") { next.farFieldCascadeCount = 2; }
        SUBCASE("enabled with quality Off")
        {
            disabled.quality = render::gi::GIQuality::Off;
            next = disabled;
            next.enabled = true;
        }
        CHECK_FALSE(giSettingsRequireRebuild(disabled, next));
    }
}

TEST_CASE("environment engine: turning weather off restores the authored fog and clouds")
{
    DispatcherScope scope;
    auto& dispatcher = events::EventDispatcher::instance();

    FakeEnvironment env;
    auto& fog = env.authored.postProcess.volumetricFog;
    fog.enabled = true;
    fog.uniformDensity = 0.02f;
    fog.heightFogDensity = 0.04f;
    fog.fogColor[0] = 0.3f;
    fog.fogColor[1] = 0.4f;
    fog.fogColor[2] = 0.5f;
    auto& cloud = env.authored.cloud;
    cloud.globalCoverage = 0.35f;
    cloud.globalDensity = 0.6f;
    cloud.cloudType = 0.2f;
    cloud.windSpeed = 12.0f;
    cloud.windDirectionDeg = 90.0f;
    cloud.cloudColorTint = glm::vec3(0.9f, 0.8f, 0.7f);
    cloud.ambientIntensity = 0.8f;
    env.livePostProcess = env.authored.postProcess;
    env.liveCloud = env.authored.cloud;
    env.registerLiveHandlers();

    services::WeatherServiceImpl weatherService;
    weatherService.registerEventHandlers();

    events::weather::SetWeatherEnabledCommand enable;
    enable.enabled = true;
    dispatcher.execute(enable);
    events::weather::SetWeatherImmediateCommand fogPreset;
    fogPreset.state = weather::getPreset(weather::WeatherPresetId::Fog);
    dispatcher.execute(fogPreset);
    events::weather::UpdateWeatherCommand update;
    dispatcher.execute(update);

    // While enabled, weather overrides the live fog density + colour and the live clouds.
    CHECK(env.livePostProcess.volumetricFog.uniformDensity == doctest::Approx(0.1f));
    CHECK(env.livePostProcess.volumetricFog.heightFogDensity == doctest::Approx(0.15f));
    CHECK(env.livePostProcess.volumetricFog.fogColor[0] == doctest::Approx(0.88f));
    CHECK(env.livePostProcess.volumetricFog.fogColor[1] == doctest::Approx(0.9f));
    CHECK(env.livePostProcess.volumetricFog.fogColor[2] == doctest::Approx(0.92f));
    CHECK(env.liveCloud.globalCoverage == doctest::Approx(0.5f));
    CHECK(env.liveCloud.windSpeed == doctest::Approx(2.0f));
    // Weather modulates the sky every frame; it must never go through the auto-Sun apply.
    CHECK(env.liveAtmosphere.ambientIntensity == doctest::Approx(0.6f)); // base 1.0 x Fog ambientLightMult
    CHECK(env.sunManagingAtmosphereApplies == 0);

    events::weather::SetWeatherEnabledCommand disable;
    disable.enabled = false;

    SUBCASE("the scene's authored values come back")
    {
        env.registerAuthoredHandler();
        dispatcher.execute(disable);

        const auto& liveFog = env.livePostProcess.volumetricFog;
        CHECK(liveFog.uniformDensity == doctest::Approx(0.02f));
        CHECK(liveFog.heightFogDensity == doctest::Approx(0.04f));
        CHECK(liveFog.fogColor[0] == doctest::Approx(0.3f));
        CHECK(liveFog.fogColor[1] == doctest::Approx(0.4f));
        CHECK(liveFog.fogColor[2] == doctest::Approx(0.5f));
        CHECK(liveFog.enabled);

        CHECK(env.liveCloud.globalCoverage == doctest::Approx(0.35f));
        CHECK(env.liveCloud.globalDensity == doctest::Approx(0.6f));
        CHECK(env.liveCloud.cloudType == doctest::Approx(0.2f));
        CHECK(env.liveCloud.windSpeed == doctest::Approx(12.0f));
        CHECK(env.liveCloud.windDirectionDeg == doctest::Approx(90.0f));
        CHECK(env.liveCloud.cloudColorTint.x == doctest::Approx(0.9f));
        CHECK(env.liveCloud.cloudColorTint.y == doctest::Approx(0.8f));
        CHECK(env.liveCloud.cloudColorTint.z == doctest::Approx(0.7f));
        CHECK(env.liveCloud.ambientIntensity == doctest::Approx(0.8f));

        // The atmosphere's captured base comes back unchanged, still without the auto-Sun apply.
        CHECK(env.liveAtmosphere.ambientIntensity == doctest::Approx(1.0f));
        CHECK(env.sunManagingAtmosphereApplies == 0);
    }

    SUBCASE("without scene settings the clear values are used")
    {
        dispatcher.execute(disable);

        CHECK(env.livePostProcess.volumetricFog.uniformDensity == 0.0f);
        CHECK(env.livePostProcess.volumetricFog.heightFogDensity == 0.0f);
        CHECK(env.liveCloud.globalCoverage == 0.0f);
        CHECK(env.liveCloud.globalDensity == 0.0f);
        CHECK(env.liveCloud.windSpeed == 0.0f);
        CHECK(env.liveCloud.ambientIntensity == 1.0f);
        CHECK(env.liveCloud.cloudColorTint == glm::vec3(1.0f));
    }
}

TEST_CASE("environment engine: GI follows the scene on load and keeps this session's debug views")
{
    DispatcherScope scope;
    auto& dispatcher = events::EventDispatcher::instance();

    FakeEnvironment env;
    env.authored.gi = GISettings::fromQuality(render::gi::GIQuality::High);
    env.authored.gi.probeSpacing = 3.0f;
    env.registerAuthoredHandler();

    FakeGIProvider provider;
    provider.live.showProbes = true;
    provider.live.showProbeValidity = true;

    services::GIServiceImpl giService(&provider);
    giService.registerEventHandlers();

    events::scene::RenderSettingsChangedNotification edit;
    edit.section = events::scene::RenderSettingsSection::Atmosphere;
    edit.sceneLoad = false;
    dispatcher.publish(edit);
    CHECK(provider.applied.empty()); // only scene loads re-apply GI

    events::scene::RenderSettingsChangedNotification load;
    load.sceneLoad = true;
    dispatcher.publish(load);

    REQUIRE(provider.applied.size() == 1);
    const auto& applied = provider.applied.front();
    CHECK(applied.enabled);
    CHECK(applied.quality == render::gi::GIQuality::High);
    CHECK(applied.probeSpacing == 3.0f);
    CHECK(applied.showProbes);
    CHECK_FALSE(applied.showCascadeBounds);
    CHECK(applied.showProbeValidity);
}
