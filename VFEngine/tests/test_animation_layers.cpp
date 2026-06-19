#include <doctest.h>
#include <animator/AnimationLayerTypes.hpp>
#include <animator/SocketTypes.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1060: Animation layer types unit tests
// ============================================================

TEST_SUITE("AnimationLayers") {

// ---- layerBlendModeToString / stringToLayerBlendMode roundtrip ----

TEST_CASE("layerBlendModeToString and stringToLayerBlendMode: Override roundtrip") {
    std::string str = animator::layerBlendModeToString(animator::LayerBlendMode::Override);
    CHECK(std::string(str) == "Override");
    CHECK(animator::stringToLayerBlendMode(str) == animator::LayerBlendMode::Override);
}

TEST_CASE("layerBlendModeToString and stringToLayerBlendMode: Additive roundtrip") {
    std::string str = animator::layerBlendModeToString(animator::LayerBlendMode::Additive);
    CHECK(std::string(str) == "Additive");
    CHECK(animator::stringToLayerBlendMode(str) == animator::LayerBlendMode::Additive);
}

// ---- layerSourceModeToString / stringToLayerSourceMode roundtrip ----

TEST_CASE("layerSourceModeToString and stringToLayerSourceMode: StateMachine roundtrip") {
    std::string str = animator::layerSourceModeToString(animator::LayerSourceMode::StateMachine);
    CHECK(std::string(str) == "StateMachine");
    CHECK(animator::stringToLayerSourceMode(str) == animator::LayerSourceMode::StateMachine);
}

TEST_CASE("layerSourceModeToString and stringToLayerSourceMode: DirectClip roundtrip") {
    std::string str = animator::layerSourceModeToString(animator::LayerSourceMode::DirectClip);
    CHECK(std::string(str) == "DirectClip");
    CHECK(animator::stringToLayerSourceMode(str) == animator::LayerSourceMode::DirectClip);
}

// ---- additiveReferencePoseToString / stringToAdditiveReferencePose roundtrip ----

TEST_CASE("additiveReferencePoseToString and stringToAdditiveReferencePose: BindPose roundtrip") {
    std::string str = animator::additiveReferencePoseToString(animator::AdditiveReferencePose::BindPose);
    CHECK(std::string(str) == "BindPose");
    CHECK(animator::stringToAdditiveReferencePose(str) == animator::AdditiveReferencePose::BindPose);
}

TEST_CASE("additiveReferencePoseToString and stringToAdditiveReferencePose: FirstFrame roundtrip") {
    std::string str = animator::additiveReferencePoseToString(animator::AdditiveReferencePose::FirstFrame);
    CHECK(std::string(str) == "FirstFrame");
    CHECK(animator::stringToAdditiveReferencePose(str) == animator::AdditiveReferencePose::FirstFrame);
}

TEST_CASE("additiveReferencePoseToString and stringToAdditiveReferencePose: SpecificFrame roundtrip") {
    std::string str = animator::additiveReferencePoseToString(animator::AdditiveReferencePose::SpecificFrame);
    CHECK(std::string(str) == "SpecificFrame");
    CHECK(animator::stringToAdditiveReferencePose(str) == animator::AdditiveReferencePose::SpecificFrame);
}

// ---- SocketDefinition::getLocalOffsetMatrix ----

TEST_CASE("SocketDefinition: zero offset produces identity-like translation") {
    animator::SocketDefinition socket;
    socket.localPosition = glm::vec3(0.0f);

    glm::mat4 mat = socket.getLocalOffsetMatrix();

    // Translation part (column 3) should be zero
    CHECK(mat[3][0] == doctest::Approx(0.0f));
    CHECK(mat[3][1] == doctest::Approx(0.0f));
    CHECK(mat[3][2] == doctest::Approx(0.0f));
    CHECK(mat[3][3] == doctest::Approx(1.0f));
}

TEST_CASE("SocketDefinition: non-zero offset has matching translation") {
    animator::SocketDefinition socket;
    socket.localPosition = glm::vec3(1.5f, -2.0f, 3.7f);

    glm::mat4 mat = socket.getLocalOffsetMatrix();

    CHECK(mat[3][0] == doctest::Approx(1.5f));
    CHECK(mat[3][1] == doctest::Approx(-2.0f));
    CHECK(mat[3][2] == doctest::Approx(3.7f));
    CHECK(mat[3][3] == doctest::Approx(1.0f));

    // Upper-left 3x3 should remain identity (no rotation/scale)
    CHECK(mat[0][0] == doctest::Approx(1.0f));
    CHECK(mat[1][1] == doctest::Approx(1.0f));
    CHECK(mat[2][2] == doctest::Approx(1.0f));
}

TEST_CASE("SocketDefinition: default localRotation is identity") {
    animator::SocketDefinition socket;
    CHECK(socket.localRotation.w == doctest::Approx(1.0f));
    CHECK(socket.localRotation.x == doctest::Approx(0.0f));
    CHECK(socket.localRotation.y == doctest::Approx(0.0f));
    CHECK(socket.localRotation.z == doctest::Approx(0.0f));
}

TEST_CASE("SocketDefinition: rotation rotates basis while preserving translation") {
    animator::SocketDefinition socket;
    socket.localPosition = glm::vec3(1.0f, 2.0f, 3.0f);
    // 90 degrees about Y maps local +X to world -Z (right-handed, column-major).
    socket.localRotation = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f)));

    glm::mat4 mat = socket.getLocalOffsetMatrix();

    // Translation column is unchanged by the rotation (rotation applied before translate).
    CHECK(mat[3][0] == doctest::Approx(1.0f));
    CHECK(mat[3][1] == doctest::Approx(2.0f));
    CHECK(mat[3][2] == doctest::Approx(3.0f));

    // Rotated +X axis (first column) should now point along -Z.
    glm::vec3 rotatedX = glm::vec3(mat[0][0], mat[0][1], mat[0][2]);
    CHECK(rotatedX.x == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(rotatedX.y == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(rotatedX.z == doctest::Approx(-1.0f).epsilon(0.001f));
}

// ---- MAX_BONE_MASK_SIZE ----

TEST_CASE("MAX_BONE_MASK_SIZE equals 256") {
    CHECK(animator::MAX_BONE_MASK_SIZE == 256);
}

TEST_CASE("BoneMask has correct size") {
    animator::BoneMask mask;
    CHECK(mask.size() == 256);
}

} // TEST_SUITE
