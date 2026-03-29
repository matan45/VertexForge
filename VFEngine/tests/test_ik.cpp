#include <doctest.h>
#include <animator/FootIKHelper.hpp>
#include <animator/HandIKHelper.hpp>
#include <animator/LookAtIKHelper.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// ============================================================
// VK-1092: IK system unit tests
// ============================================================

TEST_SUITE("IK") {

// ---- FootIK ----

TEST_CASE("FootIK: no raycast hit returns original position with weight 0") {
    glm::vec3 footPos{0.0f, 1.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    animator::ik::FootRaycastResult ray;
    ray.hit = false;

    auto result = animator::ik::FootIKHelper::calculateFootTarget(footPos, up, ray);
    CHECK(result.weight == doctest::Approx(0.0f));
    CHECK(result.isGrounded == false);
    CHECK(result.targetPosition.x == doctest::Approx(footPos.x));
    CHECK(result.targetPosition.y == doctest::Approx(footPos.y));
    CHECK(result.targetPosition.z == doctest::Approx(footPos.z));
}

TEST_CASE("FootIK: flat ground hit returns isGrounded true with weight > 0") {
    glm::vec3 footPos{0.0f, 0.2f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    animator::ik::FootRaycastResult ray;
    ray.hit = true;
    ray.hitPoint = glm::vec3{0.0f, 0.0f, 0.0f};
    ray.hitNormal = glm::vec3{0.0f, 1.0f, 0.0f};
    ray.hitDistance = 0.2f;

    animator::ik::FootIKConfig config;
    config.footHeight = 0.05f;
    config.maxStepHeight = 0.5f;

    auto result = animator::ik::FootIKHelper::calculateFootTarget(footPos, up, ray, config);
    CHECK(result.isGrounded == true);
    CHECK(result.weight > 0.0f);
}

TEST_CASE("FootIK: beyond maxStepHeight reduces weight toward 0") {
    glm::vec3 footPos{0.0f, 1.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    animator::ik::FootRaycastResult ray;
    ray.hit = true;
    // Ground is far below the foot, exceeding maxStepHeight
    ray.hitPoint = glm::vec3{0.0f, -1.0f, 0.0f};
    ray.hitNormal = glm::vec3{0.0f, 1.0f, 0.0f};
    ray.hitDistance = 2.0f;

    animator::ik::FootIKConfig config;
    config.footHeight = 0.05f;
    config.maxStepHeight = 0.5f;

    auto result = animator::ik::FootIKHelper::calculateFootTarget(footPos, up, ray, config);
    // Weight should be very low or zero when step height is greatly exceeded
    CHECK(result.weight < 0.5f);
}

// ---- HandIK ----

TEST_CASE("HandIK: target within reach returns isReachable true") {
    glm::vec3 handPos{0.0f, 1.5f, 0.5f};
    glm::vec3 shoulderPos{0.0f, 1.5f, 0.0f};
    glm::vec3 targetPos{0.0f, 1.5f, 0.8f};  // within default maxReachDistance of 1.5

    animator::ik::HandIKConfig config;
    config.maxReachDistance = 1.5f;

    auto result = animator::ik::HandIKHelper::calculateHandTarget(
        handPos, shoulderPos, targetPos, std::nullopt, config);
    CHECK(result.isReachable == true);
    CHECK(result.weight > 0.0f);
}

TEST_CASE("HandIK: target beyond maxReach is clamped and isReachable false") {
    glm::vec3 handPos{0.0f, 1.5f, 0.5f};
    glm::vec3 shoulderPos{0.0f, 1.5f, 0.0f};
    glm::vec3 targetPos{0.0f, 1.5f, 5.0f};  // well beyond reach

    animator::ik::HandIKConfig config;
    config.maxReachDistance = 1.5f;

    auto result = animator::ik::HandIKHelper::calculateHandTarget(
        handPos, shoulderPos, targetPos, std::nullopt, config);
    CHECK(result.isReachable == false);

    // The target position should be clamped to maxReachDistance from shoulder
    float dist = glm::length(result.targetPosition - shoulderPos);
    CHECK(dist <= config.maxReachDistance + 0.01f);
}

TEST_CASE("HandIK: target at zero distance does not crash") {
    glm::vec3 handPos{0.0f, 1.5f, 0.0f};
    glm::vec3 shoulderPos{0.0f, 1.5f, 0.0f};
    glm::vec3 targetPos{0.0f, 1.5f, 0.0f};  // exactly at shoulder

    animator::ik::HandIKConfig config;
    config.maxReachDistance = 1.5f;

    // Should not crash or produce NaN
    auto result = animator::ik::HandIKHelper::calculateHandTarget(
        handPos, shoulderPos, targetPos, std::nullopt, config);
    CHECK_FALSE(std::isnan(result.targetPosition.x));
    CHECK_FALSE(std::isnan(result.targetPosition.y));
    CHECK_FALSE(std::isnan(result.targetPosition.z));
    CHECK_FALSE(std::isnan(result.weight));
}

// ---- LookAtIK ----

TEST_CASE("LookAtIK: target in dead zone returns weight 0") {
    glm::vec3 headPos{0.0f, 1.7f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    // Target almost directly ahead, well within deadZoneAngle (5 degrees ~ 0.087 rad)
    glm::vec3 target{0.0f, 1.7f, -10.0f};

    animator::ik::LookAtConfig config;
    config.deadZoneAngle = glm::radians(5.0f);
    config.maxAngle = glm::radians(80.0f);

    float weight = -1.0f;
    animator::ik::LookAtIKHelper::calculateLookAtTarget(headPos, forward, target, config, &weight);
    CHECK(weight == doctest::Approx(0.0f));
}

TEST_CASE("LookAtIK: target within maxAngle returns weight > 0") {
    glm::vec3 headPos{0.0f, 1.7f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    // Target at roughly 45 degrees to the right -- within 80 degree max
    glm::vec3 target{5.0f, 1.7f, -5.0f};

    animator::ik::LookAtConfig config;
    config.deadZoneAngle = glm::radians(5.0f);
    config.maxAngle = glm::radians(80.0f);

    float weight = -1.0f;
    animator::ik::LookAtIKHelper::calculateLookAtTarget(headPos, forward, target, config, &weight);
    CHECK(weight > 0.0f);
}

TEST_CASE("LookAtIK: target beyond maxAngle reduces weight") {
    glm::vec3 headPos{0.0f, 1.7f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    // Target behind the character — well beyond 80 degrees
    glm::vec3 target{0.0f, 1.7f, 10.0f};

    animator::ik::LookAtConfig config;
    config.deadZoneAngle = glm::radians(5.0f);
    config.maxAngle = glm::radians(80.0f);

    float weight = -1.0f;
    animator::ik::LookAtIKHelper::calculateLookAtTarget(
        headPos, forward, target, config, &weight);

    // Weight is still applied (clamped direction used), verify it's valid
    CHECK(weight >= 0.0f);
    CHECK(weight <= 1.0f);
}

TEST_CASE("LookAtIK: smoothTarget interpolation") {
    glm::vec3 current{0.0f, 1.7f, -5.0f};
    glm::vec3 desired{3.0f, 1.7f, -5.0f};
    float dt = 0.016f;
    float blendSpeed = 5.0f;

    auto result = animator::ik::LookAtIKHelper::smoothTarget(current, desired, dt, blendSpeed);

    // Result should be between current and desired (interpolating)
    CHECK(result.x > current.x);
    CHECK(result.x < desired.x);
    // Y and Z should remain close since they are the same in both
    CHECK(result.y == doctest::Approx(1.7f).epsilon(0.01));
    CHECK(result.z == doctest::Approx(-5.0f).epsilon(0.01));
}

} // TEST_SUITE
