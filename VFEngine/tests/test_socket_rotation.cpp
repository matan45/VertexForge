#include <doctest.h>
#include <animator/SocketTypes.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

// ============================================================
// VK-1402: socket local rotation offset
// Verifies SocketDefinition::getLocalOffsetMatrix() composes the local
// translation and rotation as T * R (rotation first, then translation), so an
// attached weapon inherits both the bone transform and the authored offset.
// ============================================================

TEST_SUITE("SocketRotation") {

TEST_CASE("getLocalOffsetMatrix: identity rotation is pure translation") {
    animator::SocketDefinition s;
    s.localPosition = glm::vec3(5.0f, -2.0f, 3.0f);
    // localRotation defaults to identity (w=1).

    glm::mat4 m = s.getLocalOffsetMatrix();

    // Translation column carries the position.
    CHECK(m[3].x == doctest::Approx(5.0f));
    CHECK(m[3].y == doctest::Approx(-2.0f));
    CHECK(m[3].z == doctest::Approx(3.0f));

    // Upper-left 3x3 is identity (no rotation/scale).
    CHECK(m[0].x == doctest::Approx(1.0f));
    CHECK(m[1].y == doctest::Approx(1.0f));
    CHECK(m[2].z == doctest::Approx(1.0f));

    // A point transforms by translation only.
    glm::vec4 p = m * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(p.x == doctest::Approx(5.0f));
    CHECK(p.y == doctest::Approx(-2.0f));
    CHECK(p.z == doctest::Approx(4.0f));
}

TEST_CASE("getLocalOffsetMatrix: 90deg yaw rotates +Z to +X") {
    animator::SocketDefinition s;
    s.localRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 m = s.getLocalOffsetMatrix();

    glm::vec4 p = m * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(std::fabs(p.y) < 1e-4f);
    CHECK(std::fabs(p.z) < 1e-4f);
}

TEST_CASE("getLocalOffsetMatrix: rotation applied before translation (T*R)") {
    animator::SocketDefinition s;
    s.localPosition = glm::vec3(5.0f, 0.0f, 0.0f);
    s.localRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 m = s.getLocalOffsetMatrix();

    // Translation column is exactly the position, independent of rotation,
    // confirming the T*R ordering (not R*T, which would rotate the offset).
    CHECK(m[3].x == doctest::Approx(5.0f));
    CHECK(std::fabs(m[3].y) < 1e-4f);
    CHECK(std::fabs(m[3].z) < 1e-4f);

    // (0,0,1) -> rotate to (1,0,0) -> translate by (5,0,0) -> (6,0,0).
    glm::vec4 p = m * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    CHECK(p.x == doctest::Approx(6.0f));
    CHECK(std::fabs(p.y) < 1e-4f);
    CHECK(std::fabs(p.z) < 1e-4f);
}

}
