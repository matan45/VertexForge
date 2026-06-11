#include <doctest.h>
#include <types/PhysicsAnimationTypes.hpp>
#include <types/PhysicsTypes.hpp>
#include <physics/HitReactionState.hpp>
#include <physics/PhysicsAnimationAsset.hpp>
#include <nlohmann/json.hpp>

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

// ============================================================
// Powered ragdoll: motors, hit reactions, serialization
// ============================================================

// ---- PhysicsAnimationConfig::findBoneMotor ----

TEST_CASE("PhysicsAnimationConfig: findBoneMotor returns override for existing bone") {
    types::PhysicsAnimationConfig config;
    types::BoneMotorSettings motor;
    motor.boneName = "Spine";
    motor.strength = 0.4f;
    motor.maxTorque = 300.0f;
    config.boneMotors.push_back(motor);

    const auto* found = config.findBoneMotor("Spine");
    REQUIRE(found != nullptr);
    CHECK(found->strength == doctest::Approx(0.4f));
    CHECK(found->maxTorque == doctest::Approx(300.0f));
}

TEST_CASE("PhysicsAnimationConfig: findBoneMotor returns nullptr without override") {
    types::PhysicsAnimationConfig config;
    CHECK(config.findBoneMotor("Spine") == nullptr);

    types::BoneMotorSettings motor;
    motor.boneName = "Head";
    config.boneMotors.push_back(motor);
    CHECK(config.findBoneMotor("Spine") == nullptr);
}

TEST_CASE("BoneMotorSettings: defaults are sane for pose tracking") {
    types::BoneMotorSettings motor;
    CHECK(motor.strength == doctest::Approx(1.0f));
    CHECK(motor.frequency == doctest::Approx(20.0f));
    CHECK(motor.damping == doctest::Approx(1.0f));
    CHECK(motor.maxTorque > 0.0f);
}

// ---- Mode string round-trip ----

TEST_CASE("PhysicsAnimationAsset: mode string round-trip includes poweredRagdoll") {
    types::PhysicsAnimationConfig config;
    config.defaultMode = types::PhysicsAnimationMode::PoweredRagdoll;

    auto j = physics::PhysicsAnimationAsset::serializeConfig(config);
    CHECK(j["defaultMode"].get<std::string>() == "poweredRagdoll");

    auto restored = physics::PhysicsAnimationAsset::deserializeConfig(j);
    CHECK(restored.defaultMode == types::PhysicsAnimationMode::PoweredRagdoll);
}

TEST_CASE("PhysicsAnimationAsset: unknown mode string falls back to Animated") {
    nlohmann::json j;
    j["defaultMode"] = "somethingElse";
    auto config = physics::PhysicsAnimationAsset::deserializeConfig(j);
    CHECK(config.defaultMode == types::PhysicsAnimationMode::Animated);
}

// ---- .vfPhysAnim config round-trip ----

TEST_CASE("PhysicsAnimationAsset: motor config round-trip") {
    types::PhysicsAnimationConfig config;
    config.defaultMotorStrength = 0.8f;
    config.defaultMotorFrequency = 25.0f;
    config.defaultMotorDamping = 0.9f;
    config.defaultMotorMaxTorque = 220.0f;
    config.rootMotorStrength = 0.5f;
    config.poweredBlendInTime = 0.25f;
    config.ragdollToAnimatedBlendTime = 0.45f;
    config.hitReaction.defaultRecoverTime = 1.2f;
    config.hitReaction.strengthDip = 0.1f;
    config.hitReaction.chainDepth = 2;
    config.hitReaction.chainFalloff = 0.7f;
    config.settleLinearVelocityThreshold = 0.08f;
    config.settleAngularVelocityThreshold = 0.3f;
    config.settleFrameCount = 45;

    types::BoneMotorSettings spine;
    spine.boneName = "Spine";
    spine.strength = 0.6f;
    spine.frequency = 30.0f;
    spine.damping = 1.1f;
    spine.maxTorque = 400.0f;
    config.boneMotors.push_back(spine);

    auto j = physics::PhysicsAnimationAsset::serializeConfig(config);
    auto restored = physics::PhysicsAnimationAsset::deserializeConfig(j);

    CHECK(restored.defaultMotorStrength == doctest::Approx(0.8f));
    CHECK(restored.defaultMotorFrequency == doctest::Approx(25.0f));
    CHECK(restored.defaultMotorDamping == doctest::Approx(0.9f));
    CHECK(restored.defaultMotorMaxTorque == doctest::Approx(220.0f));
    CHECK(restored.rootMotorStrength == doctest::Approx(0.5f));
    CHECK(restored.poweredBlendInTime == doctest::Approx(0.25f));
    CHECK(restored.ragdollToAnimatedBlendTime == doctest::Approx(0.45f));
    CHECK(restored.hitReaction.defaultRecoverTime == doctest::Approx(1.2f));
    CHECK(restored.hitReaction.strengthDip == doctest::Approx(0.1f));
    CHECK(restored.hitReaction.chainDepth == 2);
    CHECK(restored.hitReaction.chainFalloff == doctest::Approx(0.7f));
    CHECK(restored.settleLinearVelocityThreshold == doctest::Approx(0.08f));
    CHECK(restored.settleAngularVelocityThreshold == doctest::Approx(0.3f));
    CHECK(restored.settleFrameCount == 45);

    REQUIRE(restored.boneMotors.size() == 1);
    CHECK(restored.boneMotors[0].boneName == "Spine");
    CHECK(restored.boneMotors[0].strength == doctest::Approx(0.6f));
    CHECK(restored.boneMotors[0].frequency == doctest::Approx(30.0f));
    CHECK(restored.boneMotors[0].damping == doctest::Approx(1.1f));
    CHECK(restored.boneMotors[0].maxTorque == doctest::Approx(400.0f));
}

TEST_CASE("PhysicsAnimationAsset: missing motor keys fall back to defaults") {
    nlohmann::json j;
    j["defaultMode"] = "ragdoll"; // pre-motor asset shape
    auto config = physics::PhysicsAnimationAsset::deserializeConfig(j);

    types::PhysicsAnimationConfig defaults;
    CHECK(config.defaultMotorStrength == doctest::Approx(defaults.defaultMotorStrength));
    CHECK(config.defaultMotorFrequency == doctest::Approx(defaults.defaultMotorFrequency));
    CHECK(config.rootMotorStrength == doctest::Approx(defaults.rootMotorStrength));
    CHECK(config.boneMotors.empty());
    CHECK(config.hitReaction.chainDepth == defaults.hitReaction.chainDepth);
    CHECK(config.settleFrameCount == defaults.settleFrameCount);
}

// ---- HitReactionState: chain resolution ----

namespace {
    // Synthetic humanoid physics skeleton:
    // 0 Hips, 1 Spine(0), 2 Chest(1), 3 Head(2), 4 LeftArm(2), 5 LeftHand(4),
    // 6 RightArm(2), 7 RightHand(6), 8 LeftLeg(0), 9 RightLeg(0)
    const std::vector<int> humanoidParents = {-1, 0, 1, 2, 2, 4, 2, 6, 0, 0};

    bool chainContains(const std::vector<int>& chain, int bone) {
        for (int b : chain) { if (b == bone) return true; }
        return false;
    }
}

TEST_CASE("HitReactionState: resolveChain collects hit bone and descendants") {
    auto chain = physics::HitReactionState::resolveChain(2, humanoidParents, -1);

    CHECK(chainContains(chain, 2)); // Chest
    CHECK(chainContains(chain, 3)); // Head
    CHECK(chainContains(chain, 4)); // LeftArm
    CHECK(chainContains(chain, 5)); // LeftHand
    CHECK(chainContains(chain, 6)); // RightArm
    CHECK(chainContains(chain, 7)); // RightHand
    CHECK_FALSE(chainContains(chain, 0)); // Hips
    CHECK_FALSE(chainContains(chain, 1)); // Spine
    CHECK_FALSE(chainContains(chain, 8)); // LeftLeg
    CHECK_FALSE(chainContains(chain, 9)); // RightLeg
    CHECK(chain.size() == 6);
}

TEST_CASE("HitReactionState: resolveChain honors depth limit") {
    auto depthZero = physics::HitReactionState::resolveChain(2, humanoidParents, 0);
    CHECK(depthZero.size() == 1);
    CHECK(depthZero[0] == 2);

    auto depthOne = physics::HitReactionState::resolveChain(2, humanoidParents, 1);
    CHECK(chainContains(depthOne, 2));
    CHECK(chainContains(depthOne, 3));
    CHECK(chainContains(depthOne, 4));
    CHECK(chainContains(depthOne, 6));
    CHECK_FALSE(chainContains(depthOne, 5)); // hands are depth 2
    CHECK_FALSE(chainContains(depthOne, 7));
}

TEST_CASE("HitReactionState: resolveChain from root affects whole body") {
    auto chain = physics::HitReactionState::resolveChain(0, humanoidParents, -1);
    CHECK(chain.size() == humanoidParents.size());
}

TEST_CASE("HitReactionState: resolveChain with invalid bone returns empty") {
    CHECK(physics::HitReactionState::resolveChain(-1, humanoidParents, -1).empty());
    CHECK(physics::HitReactionState::resolveChain(99, humanoidParents, -1).empty());
}

// ---- HitReactionState: recovery curve ----

TEST_CASE("HitReactionState: scale dips on hit and recovers monotonically") {
    physics::HitReactionState state;
    physics::HitReactionInstance hit;
    hit.affectedPhysicsBones = {2, 3};
    hit.recoverTime = 1.0f;
    hit.dipStrength = 0.2f;
    state.active.push_back(hit);

    CHECK(state.scaleForBone(2) == doctest::Approx(0.2f)); // t=0 -> full dip
    CHECK(state.scaleForBone(0) == doctest::Approx(1.0f)); // unaffected bone

    float previous = state.scaleForBone(2);
    for (int i = 0; i < 9; ++i) {
        state.update(0.1f);
        float current = state.scaleForBone(2);
        CHECK(current >= previous);
        previous = current;
    }

    state.update(0.2f); // past recoverTime -> instance removed
    CHECK(state.active.empty());
    CHECK(state.scaleForBone(2) == doctest::Approx(1.0f));
}

TEST_CASE("HitReactionState: overlapping reactions take per-bone minimum") {
    physics::HitReactionState state;

    physics::HitReactionInstance mild;
    mild.affectedPhysicsBones = {1, 2};
    mild.recoverTime = 1.0f;
    mild.dipStrength = 0.8f;
    state.active.push_back(mild);

    physics::HitReactionInstance severe;
    severe.affectedPhysicsBones = {2, 3};
    severe.recoverTime = 1.0f;
    severe.dipStrength = 0.1f;
    state.active.push_back(severe);

    CHECK(state.scaleForBone(1) == doctest::Approx(0.8f));
    CHECK(state.scaleForBone(2) == doctest::Approx(0.1f)); // min of both
    CHECK(state.scaleForBone(3) == doctest::Approx(0.1f));
    CHECK(state.scaleForBone(0) == doctest::Approx(1.0f));
}

TEST_CASE("HitReactionState: zero recoverTime is fully recovered immediately") {
    physics::HitReactionState state;
    physics::HitReactionInstance hit;
    hit.affectedPhysicsBones = {0};
    hit.recoverTime = 0.0f;
    hit.dipStrength = 0.0f;
    state.active.push_back(hit);

    CHECK(state.scaleForBone(0) == doctest::Approx(1.0f));
    state.update(0.016f);
    CHECK(state.active.empty());
}

// ---- resolveEffectiveStrengths ----

TEST_CASE("resolveEffectiveStrengths: composes profile, hit scale, and global") {
    physics::HitReactionState hits;
    physics::HitReactionInstance hit;
    hit.affectedPhysicsBones = {1};
    hit.recoverTime = 1.0f;
    hit.dipStrength = 0.5f;
    hits.active.push_back(hit);

    std::vector<float> profile = {1.0f, 0.8f, 0.0f};
    auto effective = physics::resolveEffectiveStrengths(profile, hits, 0.5f);

    REQUIRE(effective.size() == 3);
    CHECK(effective[0] == doctest::Approx(0.5f));         // 1.0 * 1.0 * 0.5
    CHECK(effective[1] == doctest::Approx(0.2f));         // 0.8 * 0.5 * 0.5
    CHECK(effective[2] == doctest::Approx(0.0f));         // zero profile stays zero
}

TEST_CASE("resolveEffectiveStrengths: clamps to [0,1]") {
    physics::HitReactionState hits;
    std::vector<float> profile = {1.0f, 0.5f};
    auto effective = physics::resolveEffectiveStrengths(profile, hits, 3.0f);
    CHECK(effective[0] == doctest::Approx(1.0f));
    CHECK(effective[1] == doctest::Approx(1.0f));

    auto zeroed = physics::resolveEffectiveStrengths(profile, hits, -1.0f);
    CHECK(zeroed[0] == doctest::Approx(0.0f));
    CHECK(zeroed[1] == doctest::Approx(0.0f));
}

} // TEST_SUITE
