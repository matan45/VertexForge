#include <doctest.h>
#include <vfx/VFXEventTypes.hpp>

namespace
{
    float never() { return 1.0f; }
    float always() { return 0.0f; }

    vfx::VFXEventTypeConfig enabledConfig()
    {
        vfx::VFXEventTypeConfig config;
        config.enabled = true;
        config.vfxPath = "child.vfVFX";
        return config;
    }
}

TEST_SUITE("VFXEvents") {

TEST_CASE("event spawn count clamps to supported range") {
    auto config = enabledConfig();

    config.spawnCount = 5;
    config.probability = 1.0f;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, never) == 5);

    config.spawnCount = 20;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, never) == 8);

    config.spawnCount = 0;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, never) == 1);
}

TEST_CASE("event probability gates only partial probabilities") {
    auto config = enabledConfig();
    config.spawnCount = 4;

    config.probability = 0.5f;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, always) == 4);
    CHECK(vfx::evaluateEventSpawnCount(config, 32, never) == 0);

    int calls = 0;
    config.probability = 0.0f;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, [&]() { ++calls; return always(); }) == 0);
    CHECK(calls == 0);

    config.probability = 1.0f;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, [&]() { ++calls; return never(); }) == 4);
    CHECK(calls == 0);
}

TEST_CASE("event cap headroom wins over requested count") {
    auto config = enabledConfig();
    config.spawnCount = 8;

    CHECK(vfx::evaluateEventSpawnCount(config, 2, always) == 2);
    CHECK(vfx::evaluateEventSpawnCount(config, 0, always) == 0);
}

TEST_CASE("disabled or empty event paths produce no spawn requests") {
    auto config = enabledConfig();
    config.enabled = false;
    CHECK(vfx::evaluateEventSpawnCount(config, 32, always) == 0);

    config.enabled = true;
    config.vfxPath.clear();
    CHECK(vfx::evaluateEventSpawnCount(config, 32, always) == 0);
}

TEST_CASE("event probability roll is deterministic and salted") {
    float a = vfx::eventProbabilityRoll(1234u, 7u, 2u);
    float b = vfx::eventProbabilityRoll(1234u, 7u, 2u);
    CHECK(a == doctest::Approx(b));
    CHECK(a >= 0.0f);
    CHECK(a < 1.0f);

    CHECK(vfx::eventProbabilityRoll(1235u, 7u, 2u) != doctest::Approx(a));
    CHECK(vfx::eventProbabilityRoll(1234u, 8u, 2u) != doctest::Approx(a));
    CHECK(vfx::eventProbabilityRoll(1234u, 7u, 3u) != doctest::Approx(a));
}

TEST_CASE("batch event spawn helper accumulates cap by parent") {
    auto config = enabledConfig();
    config.spawnCount = 8;

    std::vector<vfx::VFXEventSpawnInput> events(3);
    for (auto& event : events)
    {
        event.parentKey = 42;
        event.eventType = vfx::eventTypeIndex(vfx::VFXEventType::OnCollision);
        event.config = config;
        event.position = glm::vec3(1.0f, 2.0f, 3.0f);
    }

    auto requests = vfx::evaluateEventSpawns(
        events,
        [](const vfx::VFXEventSpawnInput&) { return always(); },
        [](uint32_t) { return 2u; });

    REQUIRE(requests.size() == 2);
    CHECK(requests[0].parentKey == 42);
    CHECK(requests[0].vfxPath == "child.vfVFX");
    CHECK(requests[0].position.x == doctest::Approx(1.0f));
    CHECK(requests[0].position.y == doctest::Approx(2.0f));
    CHECK(requests[0].position.z == doctest::Approx(3.0f));
}

TEST_CASE("batch event spawn helper carries inheritance payloads") {
    auto config = enabledConfig();
    config.spawnCount = 1;
    config.inheritVelocityScale = 1.5f;
    config.inheritColor = true;
    config.inheritSize = true;

    vfx::VFXEventSpawnInput event;
    event.parentKey = 1;
    event.eventType = vfx::eventTypeIndex(vfx::VFXEventType::OnCollision);
    event.config = config;
    event.velocity = glm::vec3(2.0f, 0.0f, -4.0f);
    event.color = glm::vec3(0.25f, 0.5f, 0.75f);
    event.size = 3.0f;

    auto requests = vfx::evaluateEventSpawns(
        std::vector<vfx::VFXEventSpawnInput>{event},
        [](const vfx::VFXEventSpawnInput&) { return always(); },
        [](uint32_t) { return 32u; });

    REQUIRE(requests.size() == 1);
    REQUIRE(requests[0].inheritedVelocity.has_value());
    CHECK(requests[0].inheritedVelocity->x == doctest::Approx(3.0f));
    CHECK(requests[0].inheritedVelocity->z == doctest::Approx(-6.0f));
    REQUIRE(requests[0].startColorMultiplier.has_value());
    CHECK(requests[0].startColorMultiplier->x == doctest::Approx(0.25f));
    CHECK(requests[0].startColorMultiplier->w == doctest::Approx(1.0f));
    REQUIRE(requests[0].startSizeMultiplier.has_value());
    CHECK(requests[0].startSizeMultiplier.value() == doctest::Approx(3.0f));
}

TEST_CASE("event config store and load round-trip all event fields") {
    vfx::VFXEventConfig config;
    for (uint32_t i = 0; i < vfx::VFX_EVENT_TYPE_COUNT; ++i)
    {
        auto& eventConfig = config.types[i];
        eventConfig.enabled = (i % 2) == 0;
        eventConfig.vfxPath = "child" + std::to_string(i) + ".vfVFX";
        eventConfig.spawnCount = static_cast<int32_t>(i + 1);
        eventConfig.probability = 0.25f * static_cast<float>(i);
        eventConfig.inheritVelocityScale = 0.5f * static_cast<float>(i);
        eventConfig.inheritColor = i == 1;
        eventConfig.inheritSize = i == 2;
    }
    config.lifetimeThreshold = 0.75f;

    vfx::VFXNode node;
    node.type = vfx::VFXNodeType::Emitter;
    vfx::storeEventConfigToNode(node, config);

    auto loaded = vfx::loadEventConfigFromNode(node);
    for (uint32_t i = 0; i < vfx::VFX_EVENT_TYPE_COUNT; ++i)
    {
        const auto& expected = config.types[i];
        const auto& actual = loaded.types[i];
        CHECK(actual.enabled == expected.enabled);
        CHECK(actual.vfxPath == expected.vfxPath);
        CHECK(actual.spawnCount == expected.spawnCount);
        CHECK(actual.probability == doctest::Approx(expected.probability));
        CHECK(actual.inheritVelocityScale == doctest::Approx(expected.inheritVelocityScale));
        CHECK(actual.inheritColor == expected.inheritColor);
        CHECK(actual.inheritSize == expected.inheritSize);
    }
    CHECK(loaded.lifetimeThreshold == doctest::Approx(0.75f));
}

TEST_CASE("event config load clamps out-of-range stored values") {
    vfx::VFXNode node;
    node.type = vfx::VFXNodeType::Emitter;
    node.properties["eventOnSpawnEnabled"] = vfx::VFXProperty{
        "eventOnSpawnEnabled", vfx::VFXPropertyType::Bool, true, 0.0f, 1.0f};
    node.properties["eventOnSpawnVFX"] = vfx::VFXProperty{
        "eventOnSpawnVFX", vfx::VFXPropertyType::String, std::string("child.vfVFX"), 0.0f, 0.0f};
    node.properties["eventOnSpawnCount"] = vfx::VFXProperty{
        "eventOnSpawnCount", vfx::VFXPropertyType::Int, int32_t{99}, 1.0f, 8.0f};
    node.properties["eventOnSpawnProbability"] = vfx::VFXProperty{
        "eventOnSpawnProbability", vfx::VFXPropertyType::Float, -1.0f, 0.0f, 1.0f};
    node.properties["eventOnSpawnVelInherit"] = vfx::VFXProperty{
        "eventOnSpawnVelInherit", vfx::VFXPropertyType::Float, 10.0f, 0.0f, 2.0f};

    auto loaded = vfx::loadEventConfigFromNode(node);
    const auto& spawn = loaded.types[vfx::eventTypeIndex(vfx::VFXEventType::OnSpawn)];
    CHECK(spawn.spawnCount == vfx::EventDefaults::MAX_SPAWN_COUNT);
    CHECK(spawn.probability == doctest::Approx(0.0f));
    CHECK(spawn.inheritVelocityScale == doctest::Approx(vfx::EventDefaults::MAX_INHERIT_VELOCITY_SCALE));
}

}
