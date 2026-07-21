#include <doctest.h>
#include <weather/WeatherStateMachine.hpp>
#include <weather/WeatherSchedule.hpp>
#include <weather/WeatherZoneEvaluator.hpp>
#include <weather/LightningGenerator.hpp>
#include <weather/WeatherPresets.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/WeatherComponents.hpp>
#include <components/CoreComponents.hpp>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// ============================================================
// VK-1567: deterministic coverage for the CPU-only weather subsystem.
//
// WeatherSchedule and LightningGenerator own random_device-seeded generators, so these
// tests pin invariants that hold for every seed. Weighted selection and exact strike
// cadence deliberately remain out of scope until those classes expose seed injection.
// WeatherZoneEvaluator uses the process-wide ECS registry; WeatherZones owns only the
// entities each case creates and removes them without clearing unrelated test state.
// ============================================================

namespace
{
    using namespace weather;

    constexpr float kEpsilon = 1e-5f;
    constexpr float kSpeedOfSound = 343.3f;

    void checkState(const WeatherState& actual, const WeatherState& expected,
                    float epsilon = kEpsilon)
    {
        CHECK(actual.cloudCoverage == doctest::Approx(expected.cloudCoverage).epsilon(epsilon));
        CHECK(actual.cloudDensity == doctest::Approx(expected.cloudDensity).epsilon(epsilon));
        CHECK(actual.cloudType == doctest::Approx(expected.cloudType).epsilon(epsilon));
        CHECK(actual.precipType == expected.precipType);
        CHECK(actual.precipIntensity == doctest::Approx(expected.precipIntensity).epsilon(epsilon));
        CHECK(actual.windSpeed == doctest::Approx(expected.windSpeed).epsilon(epsilon));
        CHECK(actual.windDirectionDeg == doctest::Approx(expected.windDirectionDeg).epsilon(epsilon));
        CHECK(actual.gustStrength == doctest::Approx(expected.gustStrength).epsilon(epsilon));
        CHECK(actual.gustFrequency == doctest::Approx(expected.gustFrequency).epsilon(epsilon));
        CHECK(actual.fogDensity == doctest::Approx(expected.fogDensity).epsilon(epsilon));
        CHECK(actual.heightFogDensity == doctest::Approx(expected.heightFogDensity).epsilon(epsilon));
        CHECK(actual.atmosphereTint.x == doctest::Approx(expected.atmosphereTint.x).epsilon(epsilon));
        CHECK(actual.atmosphereTint.y == doctest::Approx(expected.atmosphereTint.y).epsilon(epsilon));
        CHECK(actual.atmosphereTint.z == doctest::Approx(expected.atmosphereTint.z).epsilon(epsilon));
        CHECK(actual.ambientLightMult == doctest::Approx(expected.ambientLightMult).epsilon(epsilon));
    }

    nlohmann::json scheduleEntry(WeatherPresetId preset, float weight = 1.0f,
                                 float minDuration = 100.0f, float maxDuration = 200.0f,
                                 float transitionDuration = 12.0f,
                                 WeatherEasing easing = WeatherEasing::Linear)
    {
        return {
            {"preset", static_cast<uint8_t>(preset)},
            {"weight", weight},
            {"minDuration", minDuration},
            {"maxDuration", maxDuration},
            {"transitionDuration", transitionDuration},
            {"easing", static_cast<uint8_t>(easing)}
        };
    }

    nlohmann::json scheduleBucket(float startHour, float endHour,
                                  nlohmann::json entries)
    {
        return {
            {"startHour", startHour},
            {"endHour", endHour},
            {"entries", std::move(entries)}
        };
    }

    nlohmann::json scheduleBiome(const std::string& id, nlohmann::json buckets)
    {
        return {{"biomeId", id}, {"buckets", std::move(buckets)}};
    }

    nlohmann::json scheduleDocument(nlohmann::json biomes)
    {
        return {{"biomes", std::move(biomes)}};
    }

    std::optional<WeatherTransition> evaluateOnce(const nlohmann::json& document,
                                                  const std::string& biome,
                                                  float timeOfDay)
    {
        WeatherSchedule schedule;
        schedule.loadFromJson(document);
        schedule.setActiveBiome(biome);
        return schedule.evaluate(timeOfDay, 0.0f);
    }

    WeatherState stateWithCoverage(float coverage)
    {
        WeatherState state = presets::Clear;
        state.cloudCoverage = coverage;
        return state;
    }

    WeatherState stormState()
    {
        WeatherState state;
        state.precipType = PrecipitationType::Rain;
        state.precipIntensity = 0.9f;
        return state;
    }

    struct ThunderCapture
    {
        LightningOutput output;
        float elapsed = 0.0f;
    };

    ThunderCapture waitForThunder(LightningGenerator& generator, const glm::vec3& cameraPos,
                                  float deltaTime = 0.01f)
    {
        generator.update(0.0f, stormState(), cameraPos);
        for (int i = 0; i < 2000; ++i)
        {
            generator.update(deltaTime, stormState(), cameraPos);
            const float elapsed = static_cast<float>(i + 1) * deltaTime;
            auto output = generator.getOutput();
            if (output.shouldPlayThunder)
                return {output, elapsed};
        }
        FAIL("lightning thunder did not fire within its maximum physical delay");
        return {};
    }

    struct WeatherZones
    {
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> entities;

        bool matchingViewEmpty()
        {
            const auto view = registry.view<components::WeatherZoneComponent,
                                            components::WorldTransformComponent>();
            return view.begin() == view.end();
        }

        entt::entity addSphere(const glm::vec3& center, float radius, float falloff,
                               int priority, const WeatherState& overrideState,
                               bool active = true)
        {
            const auto entity = registry.create();
            entities.push_back(entity);

            auto& zone = registry.emplace<components::WeatherZoneComponent>(entity);
            zone.shape = components::WeatherZoneShape::Sphere;
            zone.radius = radius;
            zone.falloffDistance = falloff;
            zone.priority = priority;
            zone.overrideState = overrideState;
            zone.active = active;
            registry.emplace<components::WorldTransformComponent>(entity).worldMatrix =
                glm::translate(glm::mat4(1.0f), center);
            return entity;
        }

        entt::entity addBox(const glm::mat4& world, const glm::vec3& halfExtents,
                            float falloff, int priority, const WeatherState& overrideState,
                            bool active = true)
        {
            const auto entity = registry.create();
            entities.push_back(entity);

            auto& zone = registry.emplace<components::WeatherZoneComponent>(entity);
            zone.shape = components::WeatherZoneShape::Box;
            zone.halfExtents = halfExtents;
            zone.falloffDistance = falloff;
            zone.priority = priority;
            zone.overrideState = overrideState;
            zone.active = active;
            registry.emplace<components::WorldTransformComponent>(entity).worldMatrix = world;
            return entity;
        }

        components::WeatherZoneComponent& zoneOf(entt::entity entity)
        {
            return registry.get<components::WeatherZoneComponent>(entity);
        }

        ~WeatherZones()
        {
            for (const auto entity : entities)
            {
                if (registry.valid(entity))
                    registry.destroy(entity);
            }
        }
    };
}

TEST_SUITE("WeatherTypes")
{
    TEST_CASE("weather-state interpolation preserves both endpoints field for field")
    {
        WeatherState a = presets::Clear;
        WeatherState b = presets::HeavySnow; // 45 -> 0 does not cross the wrap seam.
        checkState(lerpWeatherState(a, b, 0.0f), a);
        checkState(lerpWeatherState(a, b, 1.0f), b);
    }

    TEST_CASE("precipitation switches at the interpolation midpoint")
    {
        WeatherState a;
        WeatherState b;
        a.precipType = PrecipitationType::Rain;
        b.precipType = PrecipitationType::Snow;
        CHECK(lerpWeatherState(a, b, 0.499f).precipType == PrecipitationType::Rain);
        CHECK(lerpWeatherState(a, b, 0.5f).precipType == PrecipitationType::Snow);
    }

    TEST_CASE("wind interpolation follows the shortest arc with a deterministic 180-degree tie")
    {
        WeatherState a;
        WeatherState b;

        a.windDirectionDeg = 350.0f;
        b.windDirectionDeg = 10.0f;
        CHECK(lerpWeatherState(a, b, 0.5f).windDirectionDeg == doctest::Approx(360.0f));

        a.windDirectionDeg = 10.0f;
        b.windDirectionDeg = 350.0f;
        CHECK(lerpWeatherState(a, b, 0.5f).windDirectionDeg == doctest::Approx(0.0f));

        a.windDirectionDeg = 0.0f;
        b.windDirectionDeg = 180.0f;
        CHECK(lerpWeatherState(a, b, 0.5f).windDirectionDeg == doctest::Approx(-90.0f));

        a.windDirectionDeg = 45.0f;
        b.windDirectionDeg = 90.0f;
        CHECK(lerpWeatherState(a, b, 0.5f).windDirectionDeg == doctest::Approx(67.5f));
    }

    TEST_CASE("all built-in preset ids resolve to their named state")
    {
        const std::array ids{
            WeatherPresetId::Clear, WeatherPresetId::Cloudy, WeatherPresetId::Overcast,
            WeatherPresetId::LightRain, WeatherPresetId::HeavyRain,
            WeatherPresetId::Thunderstorm, WeatherPresetId::LightSnow,
            WeatherPresetId::HeavySnow, WeatherPresetId::Fog, WeatherPresetId::Sandstorm
        };
        const std::array expected{
            presets::Clear, presets::Cloudy, presets::Overcast, presets::LightRain,
            presets::HeavyRain, presets::Thunderstorm, presets::LightSnow,
            presets::HeavySnow, presets::Fog, presets::Sandstorm
        };

        for (std::size_t i = 0; i < ids.size(); ++i)
            checkState(getPreset(ids[i]), expected[i]);
    }

    TEST_CASE("Custom preserves the current preset-string fallback asymmetry")
    {
        checkState(getPreset(WeatherPresetId::Custom), presets::Clear);
        CHECK(std::string(presetIdToString(WeatherPresetId::Custom)) == "Custom");
        CHECK_FALSE(stringToPresetId("Custom").has_value());
        CHECK_FALSE(stringToPresetId("Unknown").has_value());

        const std::array ids{
            WeatherPresetId::Clear, WeatherPresetId::Cloudy, WeatherPresetId::Overcast,
            WeatherPresetId::LightRain, WeatherPresetId::HeavyRain,
            WeatherPresetId::Thunderstorm, WeatherPresetId::LightSnow,
            WeatherPresetId::HeavySnow, WeatherPresetId::Fog, WeatherPresetId::Sandstorm
        };
        for (const auto id : ids)
        {
            const auto parsed = stringToPresetId(presetIdToString(id));
            REQUIRE(parsed.has_value());
            CHECK(parsed.value() == id);
        }
    }
}

TEST_SUITE("WeatherStateMachine")
{
    TEST_CASE("a fresh state machine is idle with complete progress")
    {
        WeatherStateMachine machine;
        CHECK_FALSE(machine.isTransitioning());
        CHECK(machine.getTransitionProgress() == doctest::Approx(1.0f));
    }

    TEST_CASE("setImmediate snaps and discards a previously queued transition")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::Cloudy, 1.0f);
        machine.queueTransition(presets::Fog, 1.0f);
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::HeavyRain, 1.0f);
        machine.update(1.0f);

        CHECK_FALSE(machine.isTransitioning());
        checkState(machine.getCurrentState(), presets::HeavyRain);
    }

    TEST_CASE("linear interpolation is proportional to raw progress")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::Cloudy, 10.0f, WeatherEasing::Linear);
        machine.update(2.5f);
        CHECK(machine.getTransitionProgress() == doctest::Approx(0.25f));
        checkState(machine.getCurrentState(), lerpWeatherState(presets::Clear, presets::Cloudy, 0.25f));
    }

    TEST_CASE("EaseInOut changes state progress while exposing raw progress")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::Cloudy, 10.0f, WeatherEasing::EaseInOut);
        machine.update(2.5f);
        CHECK(machine.getTransitionProgress() == doctest::Approx(0.25f));
        checkState(machine.getCurrentState(),
                   lerpWeatherState(presets::Clear, presets::Cloudy, 0.15625f));
    }

    TEST_CASE("transition duration clamps to one hundredth of a second")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::Fog, 0.0f, WeatherEasing::Linear);
        machine.update(0.005f);
        CHECK(machine.isTransitioning());
        CHECK(machine.getTransitionProgress() == doctest::Approx(0.5f));
        machine.update(0.005f);
        CHECK_FALSE(machine.isTransitioning());
        checkState(machine.getCurrentState(), presets::Fog);
    }

    TEST_CASE("overshoot snaps exactly to the target including precipitation type")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.transitionTo(presets::HeavySnow, 1.0f);
        machine.update(10.0f);
        CHECK_FALSE(machine.isTransitioning());
        checkState(machine.getCurrentState(), presets::HeavySnow);
    }

    TEST_CASE("an idle queue starts immediately and an active queue keeps only the latest request")
    {
        WeatherStateMachine machine;
        machine.setImmediate(presets::Clear);
        machine.queueTransition(presets::Cloudy, 1.0f, WeatherEasing::Linear);
        CHECK(machine.isTransitioning());

        machine.queueTransition(presets::Fog, 1.0f, WeatherEasing::Linear);
        machine.queueTransition(presets::Sandstorm, 1.0f, WeatherEasing::Linear);
        machine.update(1.0f);
        CHECK(machine.isTransitioning());
        CHECK(machine.getTransitionProgress() == doctest::Approx(0.0f));
        checkState(machine.getCurrentState(), presets::Cloudy);

        machine.update(1.0f);
        CHECK_FALSE(machine.isTransitioning());
        checkState(machine.getCurrentState(), presets::Sandstorm);
    }
}

TEST_SUITE("WeatherSchedule")
{
    TEST_CASE("empty and malformed documents produce no transition")
    {
        WeatherSchedule empty;
        CHECK_FALSE(empty.evaluate(12.0f, 0.0f).has_value());

        WeatherSchedule malformed;
        malformed.loadFromJson({{"biomes", "not-an-array"}});
        CHECK_FALSE(malformed.evaluate(12.0f, 0.0f).has_value());
    }

    TEST_CASE("schedule JSON round-trips without losing entry metadata")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("temperate", nlohmann::json::array({
                scheduleBucket(6.0f, 18.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::Cloudy, 0.25f, 64.0f, 128.0f,
                                  16.0f, WeatherEasing::EaseInOut)
                }))
            }))
        }));
        WeatherSchedule first;
        first.loadFromJson(document);
        const auto serialized = first.toJson();
        WeatherSchedule second;
        second.loadFromJson(serialized);
        CHECK(second.toJson() == serialized);
    }

    TEST_CASE("manual override freezes the countdown and duration stays within configured bounds")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::Clear, 1.0f, 100.0f, 200.0f)
                }))
            }))
        }));
        WeatherSchedule schedule;
        schedule.loadFromJson(document);
        REQUIRE(schedule.evaluate(12.0f, 0.0f).has_value());

        schedule.setManualOverride(true);
        CHECK_FALSE(schedule.evaluate(12.0f, 1000.0f).has_value());
        schedule.setManualOverride(false);
        CHECK_FALSE(schedule.evaluate(12.0f, 99.0f).has_value());
        CHECK(schedule.evaluate(12.0f, 101.0f).has_value());
    }

    TEST_CASE("time buckets are half-open and wrapping buckets cover midnight")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(6.0f, 18.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::Clear)
                })),
                scheduleBucket(18.0f, 6.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::Fog)
                }))
            }))
        }));

        for (const float hour : {6.0f, 17.999f})
        {
            const auto transition = evaluateOnce(document, "default", hour);
            REQUIRE(transition.has_value());
            checkState(transition->targetState, presets::Clear);
        }
        for (const float hour : {18.0f, 23.0f, 3.0f})
        {
            const auto transition = evaluateOnce(document, "default", hour);
            REQUIRE(transition.has_value());
            checkState(transition->targetState, presets::Fog);
        }
    }

    TEST_CASE("missing buckets and empty entry lists produce no transition")
    {
        const auto noMatch = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(6.0f, 12.0f,
                               nlohmann::json::array({scheduleEntry(WeatherPresetId::Clear)}))
            }))
        }));
        CHECK_FALSE(evaluateOnce(noMatch, "default", 18.0f).has_value());

        const auto emptyEntries = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f, nlohmann::json::array())
            }))
        }));
        CHECK_FALSE(evaluateOnce(emptyEntries, "default", 12.0f).has_value());
    }

    TEST_CASE("active biome resolves by id and an unknown id falls back to the first biome")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("first", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f,
                               nlohmann::json::array({scheduleEntry(WeatherPresetId::Clear)}))
            })),
            scheduleBiome("second", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f,
                               nlohmann::json::array({scheduleEntry(WeatherPresetId::Fog)}))
            }))
        }));

        const auto selected = evaluateOnce(document, "second", 12.0f);
        REQUIRE(selected.has_value());
        checkState(selected->targetState, presets::Fog);

        const auto fallback = evaluateOnce(document, "missing", 12.0f);
        REQUIRE(fallback.has_value());
        checkState(fallback->targetState, presets::Clear);
    }

    TEST_CASE("non-positive total weight selects the first entry deterministically")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::Overcast, 0.0f),
                    scheduleEntry(WeatherPresetId::Fog, -1.0f)
                }))
            }))
        }));
        const auto transition = evaluateOnce(document, "default", 12.0f);
        REQUIRE(transition.has_value());
        checkState(transition->targetState, presets::Overcast);
    }

    TEST_CASE("transition duration and easing come directly from the selected entry")
    {
        const auto document = scheduleDocument(nlohmann::json::array({
            scheduleBiome("default", nlohmann::json::array({
                scheduleBucket(0.0f, 24.0f, nlohmann::json::array({
                    scheduleEntry(WeatherPresetId::HeavyRain, 1.0f, 100.0f, 200.0f,
                                  37.5f, WeatherEasing::EaseInOut)
                }))
            }))
        }));
        const auto transition = evaluateOnce(document, "default", 12.0f);
        REQUIRE(transition.has_value());
        CHECK(transition->duration == doctest::Approx(37.5f));
        CHECK(transition->easing == WeatherEasing::EaseInOut);
    }
}

TEST_SUITE("LightningGenerator")
{
    TEST_CASE("only rain at or above the thunderstorm threshold generates a strike")
    {
        const glm::vec3 cameraPos{0.0f};
        WeatherState state;
        LightningGenerator clear;
        clear.update(0.0f, state, cameraPos);
        CHECK(clear.getOutput().flashIntensity == doctest::Approx(0.0f));

        state.precipType = PrecipitationType::Rain;
        state.precipIntensity = 0.89f;
        LightningGenerator weakRain;
        weakRain.update(0.0f, state, cameraPos);
        CHECK(weakRain.getOutput().flashIntensity == doctest::Approx(0.0f));

        state.precipType = PrecipitationType::Snow;
        state.precipIntensity = 1.0f;
        LightningGenerator snow;
        snow.update(0.0f, state, cameraPos);
        CHECK(snow.getOutput().flashIntensity == doctest::Approx(0.0f));

        LightningGenerator storm;
        storm.update(0.0f, stormState(), cameraPos);
        CHECK(storm.getOutput().flashIntensity == doctest::Approx(1.0f));
        CHECK_FALSE(storm.getOutput().shouldPlayThunder);
    }

    TEST_CASE("flash intensity decays exponentially and cuts off at three tenths")
    {
        LightningGenerator generator;
        generator.update(0.0f, stormState(), glm::vec3(0.0f));
        CHECK(generator.getOutput().flashIntensity == doctest::Approx(1.0f));
        generator.update(0.1f, stormState(), glm::vec3(0.0f));
        CHECK(generator.getOutput().flashIntensity == doctest::Approx(std::exp(-1.0f)));
        generator.update(0.2f, stormState(), glm::vec3(0.0f));
        CHECK(generator.getOutput().flashIntensity == doctest::Approx(0.0f));
    }

    TEST_CASE("thunder delay and strike geometry agree with the generated position")
    {
        LightningGenerator generator;
        const glm::vec3 cameraPos{100.0f, 0.0f, -200.0f};
        const auto capture = waitForThunder(generator, cameraPos);
        const glm::vec2 horizontalDelta{
            capture.output.thunderPosition.x - cameraPos.x,
            capture.output.thunderPosition.z - cameraPos.z
        };
        const float horizontalDistance = glm::length(horizontalDelta);
        const float expectedDelay = horizontalDistance / kSpeedOfSound;

        CHECK(capture.output.thunderPosition.y - cameraPos.y == doctest::Approx(2000.0f));
        CHECK(horizontalDistance >= 200.0f - 0.01f);
        CHECK(horizontalDistance <= 5000.0f + 0.01f);
        CHECK(capture.elapsed + kEpsilon >= expectedDelay);
        CHECK(capture.elapsed <= expectedDelay + 0.01f + kEpsilon);

        generator.update(0.01f, stormState(), cameraPos);
        CHECK_FALSE(generator.getOutput().shouldPlayThunder);
    }

    TEST_CASE("thunder sound indices survive reset and cycle zero one two zero")
    {
        LightningGenerator generator;
        const glm::vec3 cameraPos{0.0f};
        std::array<int, 4> observed{};

        for (std::size_t i = 0; i < observed.size(); ++i)
        {
            observed[i] = waitForThunder(generator, cameraPos, 0.05f).output.thunderSoundIndex;
            WeatherState clear;
            generator.update(0.0f, clear, cameraPos);
            CHECK(generator.getOutput().flashIntensity == doctest::Approx(0.0f));
            CHECK_FALSE(generator.getOutput().shouldPlayThunder);
        }
        CHECK((observed == std::array<int, 4>{0, 1, 2, 0}));

        generator.update(0.0f, stormState(), cameraPos);
        CHECK(generator.getOutput().flashIntensity == doctest::Approx(1.0f));
    }
}

TEST_SUITE("WeatherZones")
{
    TEST_CASE("no zones returns the global state unchanged")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        WeatherZoneEvaluator evaluator;
        checkState(evaluator.evaluate(glm::vec3(0.0f), presets::Clear), presets::Clear);
        CHECK(evaluator.getTransitions().empty());
    }

    TEST_CASE("inactive zones do not affect output or emit transitions")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        scope.addSphere(glm::vec3(0.0f), 20.0f, 5.0f, 10,
                        stateWithCoverage(1.0f), false);
        WeatherZoneEvaluator evaluator;
        checkState(evaluator.evaluate(glm::vec3(0.0f), stateWithCoverage(0.0f)),
                   stateWithCoverage(0.0f));
        CHECK(evaluator.getTransitions().empty());
    }

    TEST_CASE("sphere falloff blends inside the shell and excludes its outer boundary")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        const auto entity = scope.addSphere(glm::vec3(0.0f), 20.0f, 5.0f, 0,
                                            stateWithCoverage(1.0f));
        WeatherZoneEvaluator evaluator;
        const auto global = stateWithCoverage(0.0f);

        CHECK(evaluator.evaluate(glm::vec3(22.5f, 0.0f, 0.0f), global).cloudCoverage ==
              doctest::Approx(0.5f));
        CHECK(scope.zoneOf(entity).currentBlendWeight == doctest::Approx(0.5f));

        checkState(evaluator.evaluate(glm::vec3(25.0f, 0.0f, 0.0f), global), global);
        CHECK(scope.zoneOf(entity).currentBlendWeight == doctest::Approx(0.0f));

        checkState(evaluator.evaluate(glm::vec3(0.0f), global), stateWithCoverage(1.0f));
        CHECK(scope.zoneOf(entity).currentBlendWeight == doctest::Approx(1.0f));
    }

    TEST_CASE("box distance uses half extents and the inverse world transform")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        const auto global = stateWithCoverage(0.0f);
        const auto overrideState = stateWithCoverage(1.0f);
        const auto identity = scope.addBox(glm::mat4(1.0f), glm::vec3(10.0f), 5.0f,
                                           0, overrideState);
        WeatherZoneEvaluator evaluator;

        checkState(evaluator.evaluate(glm::vec3(0.0f), global), overrideState);
        CHECK(scope.zoneOf(identity).currentBlendWeight == doctest::Approx(1.0f));
        CHECK(evaluator.evaluate(glm::vec3(12.0f, 0.0f, 0.0f), global).cloudCoverage ==
              doctest::Approx(0.6f));

        scope.zoneOf(identity).active = false;
        const auto translated = scope.addBox(
            glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f)),
            glm::vec3(10.0f), 5.0f, 0, overrideState);
        CHECK(evaluator.evaluate(glm::vec3(112.0f, 0.0f, 0.0f), global).cloudCoverage ==
              doctest::Approx(0.6f));
        CHECK(scope.zoneOf(translated).currentBlendWeight == doctest::Approx(0.6f));
    }

    TEST_CASE("higher priority wins even when its blend weight is lower")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        scope.addSphere(glm::vec3(0.0f), 20.0f, 5.0f, 10, stateWithCoverage(1.0f));
        scope.addSphere(glm::vec3(0.0f), 30.0f, 5.0f, 0, stateWithCoverage(0.25f));
        WeatherZoneEvaluator evaluator;
        const auto result = evaluator.evaluate(glm::vec3(22.5f, 0.0f, 0.0f),
                                               stateWithCoverage(0.0f));
        CHECK(result.cloudCoverage == doctest::Approx(0.5f));
    }

    TEST_CASE("zone enter and exit transitions emit once per boundary crossing")
    {
        WeatherZones scope;
        REQUIRE(scope.matchingViewEmpty());
        const auto entity = scope.addSphere(glm::vec3(0.0f), 20.0f, 5.0f, 0,
                                            stateWithCoverage(1.0f));
        WeatherZoneEvaluator evaluator;

        evaluator.evaluate(glm::vec3(0.0f), presets::Clear);
        REQUIRE(evaluator.getTransitions().size() == 1);
        CHECK(evaluator.getTransitions()[0].entityId == static_cast<uint32_t>(entity));
        CHECK(evaluator.getTransitions()[0].entered);

        evaluator.evaluate(glm::vec3(0.0f), presets::Clear);
        CHECK(evaluator.getTransitions().empty());

        evaluator.evaluate(glm::vec3(30.0f, 0.0f, 0.0f), presets::Clear);
        REQUIRE(evaluator.getTransitions().size() == 1);
        CHECK(evaluator.getTransitions()[0].entityId == static_cast<uint32_t>(entity));
        CHECK_FALSE(evaluator.getTransitions()[0].entered);
    }
}
