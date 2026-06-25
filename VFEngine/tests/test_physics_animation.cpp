#include <doctest.h>
#include <types/PhysicsAnimationTypes.hpp>
#include <types/PhysicsTypes.hpp>
#include <physics/HitReactionState.hpp>
#include <physics/RagdollSafety.hpp>
#include <physics/PhysicsAnimationAsset.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <limits>

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

// ============================================================
// RagdollSafety: impulse / escape-velocity guard (regression for the
// broad-phase NaN crash when a hit-reaction impulse flung a bone to infinity)
// ============================================================

TEST_CASE("RagdollSafety: clampVelocityMagnitude leaves under-cap velocities unchanged") {
    glm::vec3 v = physics::clampVelocityMagnitude(glm::vec3(1.0f, 2.0f, 2.0f), 30.0f); // length 3
    CHECK(v.x == doctest::Approx(1.0f));
    CHECK(v.y == doctest::Approx(2.0f));
    CHECK(v.z == doctest::Approx(2.0f));
}

TEST_CASE("RagdollSafety: clampVelocityMagnitude caps magnitude and preserves direction") {
    glm::vec3 axis = physics::clampVelocityMagnitude(glm::vec3(0.0f, 1000.0f, 0.0f), 30.0f);
    CHECK(glm::length(axis) == doctest::Approx(30.0f));
    CHECK(axis.y == doctest::Approx(30.0f));

    glm::vec3 diag = physics::clampVelocityMagnitude(glm::vec3(300.0f, 300.0f, 300.0f), 30.0f);
    CHECK(glm::length(diag) == doctest::Approx(30.0f));
    CHECK(diag.x == doctest::Approx(diag.y));
    CHECK(diag.y == doctest::Approx(diag.z));
}

TEST_CASE("RagdollSafety: clampVelocityMagnitude zeroes non-finite input (broad-phase guard)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    CHECK(physics::clampVelocityMagnitude(glm::vec3(nan, 0.0f, 0.0f), 30.0f) == glm::vec3(0.0f));
    CHECK(physics::clampVelocityMagnitude(glm::vec3(0.0f, inf, 0.0f), 30.0f) == glm::vec3(0.0f));
    CHECK(physics::clampVelocityMagnitude(glm::vec3(-inf, nan, 1.0f), 30.0f) == glm::vec3(0.0f));
}

TEST_CASE("RagdollSafety: clampVelocityMagnitude keeps zero at zero (no divide-by-zero)") {
    CHECK(physics::clampVelocityMagnitude(glm::vec3(0.0f), 30.0f) == glm::vec3(0.0f));
}

// ============================================================
// PhysicsAnimationAsset: file + serializer round-trips
// ============================================================

namespace {
    // Helper: a temp directory dedicated to these file-based cases.
    std::filesystem::path physAnimTempDir() {
        return std::filesystem::temp_directory_path() / "vf_physanim_test";
    }

    void removePhysAnimTemp(const std::filesystem::path& p) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
}

TEST_CASE("PhysicsAnimationAsset: save/load round-trip through a real file") {
    types::PhysicsAnimationConfig config;
    config.defaultMode = types::PhysicsAnimationMode::PoweredRagdoll;
    config.collisionLayer = 4;
    config.kinematicToRagdollBlendTime = 0.33f;
    config.defaultMotorStrength = 0.7f;
    config.rootMotorStrength = 0.42f;
    config.poweredBlendInTime = 0.22f;
    config.settleFrameCount = 50;

    types::BoneBodyMapping hips;
    hips.boneName = "Hips";
    hips.shape = types::ColliderShape::Box;
    hips.size = glm::vec3(0.2f, 0.15f, 0.25f);
    hips.mass = 6.0f;
    config.boneBodyMappings.push_back(hips);

    types::BoneBodyMapping spine;
    spine.boneName = "Spine";
    spine.shape = types::ColliderShape::Capsule;
    spine.mass = 3.5f;
    spine.friction = 0.4f;
    config.boneBodyMappings.push_back(spine);

    types::JointConstraintLimits spineLimit;
    spineLimit.boneName = "Spine";
    spineLimit.swingNormalHalfAngle = 0.5f;
    spineLimit.maxFrictionTorque = 12.5f;
    config.jointLimits.push_back(spineLimit);

    types::BoneMotorSettings spineMotor;
    spineMotor.boneName = "Spine";
    spineMotor.strength = 0.65f;
    spineMotor.frequency = 28.0f;
    spineMotor.damping = 1.05f;
    spineMotor.maxTorque = 360.0f;
    config.boneMotors.push_back(spineMotor);

    const auto dir = physAnimTempDir();
    const auto path = dir / "roundtrip_save_load.vfPhysAnim";

    REQUIRE(physics::PhysicsAnimationAsset::save(path.string(), config));

    auto loaded = physics::PhysicsAnimationAsset::load(path.string());
    REQUIRE(loaded.has_value());

    CHECK(loaded->defaultMode == types::PhysicsAnimationMode::PoweredRagdoll);
    CHECK(loaded->collisionLayer == 4);
    CHECK(loaded->kinematicToRagdollBlendTime == doctest::Approx(0.33f));
    CHECK(loaded->defaultMotorStrength == doctest::Approx(0.7f));
    CHECK(loaded->rootMotorStrength == doctest::Approx(0.42f));
    CHECK(loaded->poweredBlendInTime == doctest::Approx(0.22f));
    CHECK(loaded->settleFrameCount == 50);

    REQUIRE(loaded->boneBodyMappings.size() == 2);
    const auto* loadedHips = loaded->findMapping("Hips");
    REQUIRE(loadedHips != nullptr);
    CHECK(loadedHips->shape == types::ColliderShape::Box);
    CHECK(loadedHips->size.x == doctest::Approx(0.2f));
    CHECK(loadedHips->size.z == doctest::Approx(0.25f));
    CHECK(loadedHips->mass == doctest::Approx(6.0f));

    REQUIRE(loaded->jointLimits.size() == 1);
    const auto* loadedLimit = loaded->findJointLimits("Spine");
    REQUIRE(loadedLimit != nullptr);
    CHECK(loadedLimit->swingNormalHalfAngle == doctest::Approx(0.5f));
    CHECK(loadedLimit->maxFrictionTorque == doctest::Approx(12.5f));

    REQUIRE(loaded->boneMotors.size() == 1);
    const auto* loadedMotor = loaded->findBoneMotor("Spine");
    REQUIRE(loadedMotor != nullptr);
    CHECK(loadedMotor->strength == doctest::Approx(0.65f));
    CHECK(loadedMotor->frequency == doctest::Approx(28.0f));
    CHECK(loadedMotor->damping == doctest::Approx(1.05f));
    CHECK(loadedMotor->maxTorque == doctest::Approx(360.0f));

    // Cleanup
    removePhysAnimTemp(path);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("PhysicsAnimationAsset: boneBodyMappings round-trip preserves all three shapes") {
    types::PhysicsAnimationConfig config;

    types::BoneBodyMapping boxMap;
    boxMap.boneName = "BoxBone";
    boxMap.shape = types::ColliderShape::Box;
    boxMap.size = glm::vec3(0.3f, 0.4f, 0.5f);
    boxMap.offset = glm::vec3(0.01f, 0.02f, 0.03f);
    boxMap.rotationOffset = glm::quat(0.92388f, 0.38268f, 0.0f, 0.0f); // ~45deg about X
    boxMap.mass = 2.0f;
    boxMap.friction = 0.3f;
    boxMap.restitution = 0.1f;
    config.boneBodyMappings.push_back(boxMap);

    types::BoneBodyMapping sphereMap;
    sphereMap.boneName = "SphereBone";
    sphereMap.shape = types::ColliderShape::Sphere;
    sphereMap.size = glm::vec3(0.6f, 0.6f, 0.6f);
    sphereMap.offset = glm::vec3(0.1f, 0.2f, 0.3f);
    sphereMap.rotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    sphereMap.mass = 1.5f;
    sphereMap.friction = 0.6f;
    sphereMap.restitution = 0.2f;
    config.boneBodyMappings.push_back(sphereMap);

    types::BoneBodyMapping capsuleMap;
    capsuleMap.boneName = "CapsuleBone";
    capsuleMap.shape = types::ColliderShape::Capsule;
    capsuleMap.size = glm::vec3(0.05f, 0.7f, 0.05f);
    capsuleMap.offset = glm::vec3(-0.1f, 0.0f, 0.0f);
    capsuleMap.rotationOffset = glm::quat(0.70711f, 0.0f, 0.70711f, 0.0f); // ~90deg about Y
    capsuleMap.mass = 4.0f;
    capsuleMap.friction = 0.45f;
    capsuleMap.restitution = 0.05f;
    config.boneBodyMappings.push_back(capsuleMap);

    auto j = physics::PhysicsAnimationAsset::serializeConfig(config);
    auto restored = physics::PhysicsAnimationAsset::deserializeConfig(j);

    REQUIRE(restored.boneBodyMappings.size() == 3);

    const auto& rBox = restored.boneBodyMappings[0];
    CHECK(rBox.boneName == "BoxBone");
    CHECK(rBox.shape == types::ColliderShape::Box);
    CHECK(rBox.size.y == doctest::Approx(0.4f));
    CHECK(rBox.offset.z == doctest::Approx(0.03f));
    CHECK(rBox.mass == doctest::Approx(2.0f));
    CHECK(rBox.rotationOffset.w == doctest::Approx(0.92388f));
    CHECK(rBox.rotationOffset.x == doctest::Approx(0.38268f));
    CHECK(rBox.rotationOffset.y == doctest::Approx(0.0f));
    CHECK(rBox.rotationOffset.z == doctest::Approx(0.0f));

    const auto& rSphere = restored.boneBodyMappings[1];
    CHECK(rSphere.boneName == "SphereBone");
    CHECK(rSphere.shape == types::ColliderShape::Sphere);
    CHECK(rSphere.friction == doctest::Approx(0.6f));
    CHECK(rSphere.restitution == doctest::Approx(0.2f));

    const auto& rCapsule = restored.boneBodyMappings[2];
    CHECK(rCapsule.boneName == "CapsuleBone");
    CHECK(rCapsule.shape == types::ColliderShape::Capsule);
    CHECK(rCapsule.size.y == doctest::Approx(0.7f));
    CHECK(rCapsule.mass == doctest::Approx(4.0f));
    CHECK(rCapsule.rotationOffset.w == doctest::Approx(0.70711f));
    CHECK(rCapsule.rotationOffset.y == doctest::Approx(0.70711f));
}

TEST_CASE("PhysicsAnimationAsset: jointLimits round-trip") {
    types::PhysicsAnimationConfig config;

    types::JointConstraintLimits a;
    a.boneName = "LeftShoulder";
    a.swingNormalHalfAngle = 1.0472f; // 60deg
    a.swingPlaneHalfAngle = 0.5236f;  // 30deg
    a.twistMinAngle = -0.2618f;       // -15deg
    a.twistMaxAngle = 0.2618f;        // 15deg
    a.maxFrictionTorque = 5.0f;
    config.jointLimits.push_back(a);

    types::JointConstraintLimits b;
    b.boneName = "RightKnee";
    b.swingNormalHalfAngle = 0.1745f; // 10deg
    b.swingPlaneHalfAngle = 0.0873f;  // 5deg
    b.twistMinAngle = 0.0f;
    b.twistMaxAngle = 1.5708f;        // 90deg
    b.maxFrictionTorque = 20.0f;
    config.jointLimits.push_back(b);

    auto j = physics::PhysicsAnimationAsset::serializeConfig(config);
    auto restored = physics::PhysicsAnimationAsset::deserializeConfig(j);

    REQUIRE(restored.jointLimits.size() == 2);

    CHECK(restored.jointLimits[0].boneName == "LeftShoulder");
    CHECK(restored.jointLimits[0].swingNormalHalfAngle == doctest::Approx(1.0472f));
    CHECK(restored.jointLimits[0].swingPlaneHalfAngle == doctest::Approx(0.5236f));
    CHECK(restored.jointLimits[0].twistMinAngle == doctest::Approx(-0.2618f));
    CHECK(restored.jointLimits[0].twistMaxAngle == doctest::Approx(0.2618f));
    CHECK(restored.jointLimits[0].maxFrictionTorque == doctest::Approx(5.0f));

    CHECK(restored.jointLimits[1].boneName == "RightKnee");
    CHECK(restored.jointLimits[1].swingNormalHalfAngle == doctest::Approx(0.1745f));
    CHECK(restored.jointLimits[1].twistMaxAngle == doctest::Approx(1.5708f));
    CHECK(restored.jointLimits[1].maxFrictionTorque == doctest::Approx(20.0f));
}

TEST_CASE("PhysicsAnimationAsset: per-bone collisionLayer 255 is treated as global") {
    types::PhysicsAnimationConfig config;

    types::BoneBodyMapping globalMap;
    globalMap.boneName = "GlobalBone";
    // collisionLayer left at its default 255
    config.boneBodyMappings.push_back(globalMap);

    types::BoneBodyMapping explicitMap;
    explicitMap.boneName = "ExplicitBone";
    explicitMap.collisionLayer = 3;
    config.boneBodyMappings.push_back(explicitMap);

    auto j = physics::PhysicsAnimationAsset::serializeConfig(config);

    // The 255 (global) mapping must omit the "collisionLayer" key entirely.
    REQUIRE(j.contains("boneBodyMappings"));
    REQUIRE(j["boneBodyMappings"].size() == 2);
    CHECK_FALSE(j["boneBodyMappings"][0].contains("collisionLayer"));
    CHECK(j["boneBodyMappings"][1].contains("collisionLayer"));

    auto restored = physics::PhysicsAnimationAsset::deserializeConfig(j);
    REQUIRE(restored.boneBodyMappings.size() == 2);
    CHECK(restored.boneBodyMappings[0].collisionLayer == 255);
    CHECK(restored.boneBodyMappings[1].collisionLayer == 3);
}

TEST_CASE("PhysicsAnimationAsset: load returns nullopt for a missing file") {
    const auto path = physAnimTempDir() / "this_file_does_not_exist_12345.vfPhysAnim";
    std::error_code ec;
    REQUIRE_FALSE(std::filesystem::exists(path, ec)); // sanity: truly absent

    auto result = physics::PhysicsAnimationAsset::load(path.string());
    CHECK_FALSE(result.has_value());
}

TEST_CASE("PhysicsAnimationAsset: load returns nullopt when config object is absent") {
    const auto dir = physAnimTempDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / "no_config_object.vfPhysAnim";

    {
        std::ofstream out(path);
        REQUIRE(out.is_open());
        out << R"({"format":"VertexForge.PhysicsAnimationConfig","version":"1.1"})";
    }

    auto result = physics::PhysicsAnimationAsset::load(path.string());
    CHECK_FALSE(result.has_value());

    removePhysAnimTemp(path);
    std::filesystem::remove_all(dir, ec);
}

} // TEST_SUITE
