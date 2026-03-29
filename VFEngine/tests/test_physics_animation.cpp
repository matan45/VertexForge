#include <doctest.h>
#include <types/PhysicsAnimationTypes.hpp>
#include <types/PhysicsTypes.hpp>

// ============================================================
// VK-1093: Physics Animation unit tests
// ============================================================

TEST_SUITE("PhysicsAnimation") {

// ---- PhysicsAnimationConfig::findMapping ----

TEST_CASE("PhysicsAnimationConfig: findMapping returns pointer for existing bone") {
    types::PhysicsAnimationConfig config;
    types::BoneBodyMapping mapping;
    mapping.boneName = "Spine";
    mapping.mass = 2.5f;
    config.boneBodyMappings.push_back(mapping);

    const auto* found = config.findMapping("Spine");
    REQUIRE(found != nullptr);
    CHECK(found->boneName == "Spine");
    CHECK(found->mass == doctest::Approx(2.5f));
}

TEST_CASE("PhysicsAnimationConfig: findMapping returns nullptr for unknown bone") {
    types::PhysicsAnimationConfig config;
    types::BoneBodyMapping mapping;
    mapping.boneName = "LeftArm";
    config.boneBodyMappings.push_back(mapping);

    CHECK(config.findMapping("RightArm") == nullptr);
    CHECK(config.findMapping("") == nullptr);
}

TEST_CASE("PhysicsAnimationConfig: findMapping with multiple bones") {
    types::PhysicsAnimationConfig config;
    for (const auto& name : {"Hips", "Spine", "Head", "LeftFoot"}) {
        types::BoneBodyMapping m;
        m.boneName = name;
        config.boneBodyMappings.push_back(m);
    }

    CHECK(config.findMapping("Hips") != nullptr);
    CHECK(config.findMapping("Head") != nullptr);
    CHECK(config.findMapping("LeftFoot") != nullptr);
    CHECK(config.findMapping("RightFoot") == nullptr);
}

// ---- JointConstraintLimits defaults ----

TEST_CASE("JointConstraintLimits: default swing and twist angles are valid") {
    types::JointConstraintLimits limits;

    CHECK(limits.swingNormalHalfAngle > 0.0f);
    CHECK(limits.swingPlaneHalfAngle > 0.0f);
    CHECK(limits.twistMinAngle < limits.twistMaxAngle);
    // Default is pi/4 (~0.7854)
    CHECK(limits.swingNormalHalfAngle == doctest::Approx(0.7854f));
    CHECK(limits.swingPlaneHalfAngle == doctest::Approx(0.7854f));
    CHECK(limits.twistMinAngle == doctest::Approx(-0.7854f));
    CHECK(limits.twistMaxAngle == doctest::Approx(0.7854f));
}

TEST_CASE("JointConstraintLimits: maxFrictionTorque defaults to zero") {
    types::JointConstraintLimits limits;
    CHECK(limits.maxFrictionTorque == doctest::Approx(0.0f));
}

// ---- BoneBodyMapping defaults ----

TEST_CASE("BoneBodyMapping: default mass is positive") {
    types::BoneBodyMapping mapping;
    CHECK(mapping.mass > 0.0f);
    CHECK(mapping.mass == doctest::Approx(1.0f));
}

TEST_CASE("BoneBodyMapping: default friction is in [0,1]") {
    types::BoneBodyMapping mapping;
    CHECK(mapping.friction >= 0.0f);
    CHECK(mapping.friction <= 1.0f);
    CHECK(mapping.friction == doctest::Approx(0.5f));
}

TEST_CASE("BoneBodyMapping: default restitution is non-negative") {
    types::BoneBodyMapping mapping;
    CHECK(mapping.restitution >= 0.0f);
    CHECK(mapping.restitution == doctest::Approx(0.0f));
}

TEST_CASE("BoneBodyMapping: default shape is Capsule") {
    types::BoneBodyMapping mapping;
    CHECK(mapping.shape == types::ColliderShape::Capsule);
}

// ---- PhysicsSettings::createDefault ----

TEST_CASE("PhysicsSettings: createDefault has non-zero gravity") {
    auto settings = types::PhysicsSettings::createDefault();
    CHECK(glm::length(settings.gravity) > 0.0f);
    CHECK(settings.gravity.y == doctest::Approx(-9.81f));
}

TEST_CASE("PhysicsSettings: createDefault has positive fixedTimestep") {
    auto settings = types::PhysicsSettings::createDefault();
    CHECK(settings.fixedTimestep > 0.0);
    CHECK(settings.fixedTimestep == doctest::Approx(1.0 / 60.0));
}

TEST_CASE("PhysicsSettings: createDefault has built-in layers") {
    auto settings = types::PhysicsSettings::createDefault();
    CHECK(settings.layers.size() >= 4);

    auto* staticLayer = settings.getLayerByName("Static");
    auto* dynamicLayer = settings.getLayerByName("Dynamic");
    REQUIRE(staticLayer != nullptr);
    REQUIRE(dynamicLayer != nullptr);
    CHECK(staticLayer->isBuiltIn);
    CHECK(dynamicLayer->isBuiltIn);
}

// ---- Collision matrix symmetry ----

TEST_CASE("PhysicsSettings: setLayerCollision is symmetric") {
    types::PhysicsSettings settings;
    settings.setLayerCollision(1, 3, true);

    CHECK(settings.shouldLayersCollide(1, 3));
    CHECK(settings.shouldLayersCollide(3, 1));

    settings.setLayerCollision(1, 3, false);
    CHECK_FALSE(settings.shouldLayersCollide(1, 3));
    CHECK_FALSE(settings.shouldLayersCollide(3, 1));
}

TEST_CASE("PhysicsSettings: setLayerCollision with out-of-range layers is safe") {
    types::PhysicsSettings settings;
    // Should not crash
    settings.setLayerCollision(20, 5, true);
    CHECK_FALSE(settings.shouldLayersCollide(20, 5));
}

// ---- Default collision: Static vs Dynamic ----

TEST_CASE("PhysicsSettings: default Static collides with Dynamic") {
    auto settings = types::PhysicsSettings::createDefault();
    // Static=0, Dynamic=1
    CHECK(settings.shouldLayersCollide(0, 1));
    CHECK(settings.shouldLayersCollide(1, 0));
}

TEST_CASE("PhysicsSettings: default Static does not collide with Sensor") {
    auto settings = types::PhysicsSettings::createDefault();
    // Static=0, Sensor=3
    CHECK_FALSE(settings.shouldLayersCollide(0, 3));
}

} // TEST_SUITE
