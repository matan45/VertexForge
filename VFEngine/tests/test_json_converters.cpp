#include <doctest.h>
#include <serialization/JsonConverters.hpp>
#include <nlohmann/json.hpp>

// ============================================================
// JSON converter roundtrip tests for GLM vector types
// ============================================================

TEST_SUITE("JsonConverters") {

// ---- vec3 to_json / from_json ----

TEST_CASE("vec3: roundtrip preserves values") {
    glm::vec3 original{1.0f, 2.0f, 3.0f};
    nlohmann::json j;
    serialization::to_json(j, original);

    glm::vec3 restored;
    serialization::from_json(j, restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
    CHECK(restored.z == doctest::Approx(original.z));
}

TEST_CASE("vec3: zero vector roundtrip") {
    glm::vec3 original{0.0f, 0.0f, 0.0f};
    nlohmann::json j;
    serialization::to_json(j, original);

    glm::vec3 restored;
    serialization::from_json(j, restored);

    CHECK(restored.x == doctest::Approx(0.0f));
    CHECK(restored.y == doctest::Approx(0.0f));
    CHECK(restored.z == doctest::Approx(0.0f));
}

TEST_CASE("vec3: negative values roundtrip") {
    glm::vec3 original{-5.5f, -100.0f, -0.001f};
    nlohmann::json j;
    serialization::to_json(j, original);

    glm::vec3 restored;
    serialization::from_json(j, restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
    CHECK(restored.z == doctest::Approx(original.z));
}

TEST_CASE("vec3: large values roundtrip") {
    glm::vec3 original{1e6f, -1e6f, 999999.0f};
    nlohmann::json j;
    serialization::to_json(j, original);

    glm::vec3 restored;
    serialization::from_json(j, restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
    CHECK(restored.z == doctest::Approx(original.z));
}

// ---- writeVec2 / readVec2 ----

TEST_CASE("vec2: writeVec2/readVec2 roundtrip") {
    glm::vec2 original{3.14f, -2.71f};
    nlohmann::json j;
    j["uv"] = serialization::writeVec2(original);

    glm::vec2 restored{0.0f, 0.0f};
    serialization::readVec2(j, "uv", restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
}

TEST_CASE("vec2: zero vector roundtrip") {
    glm::vec2 original{0.0f, 0.0f};
    nlohmann::json j;
    j["pos"] = serialization::writeVec2(original);

    glm::vec2 restored{1.0f, 1.0f};
    serialization::readVec2(j, "pos", restored);

    CHECK(restored.x == doctest::Approx(0.0f));
    CHECK(restored.y == doctest::Approx(0.0f));
}

TEST_CASE("vec2: missing key leaves output unchanged") {
    nlohmann::json j;
    j["other"] = 42;

    glm::vec2 out{7.0f, 8.0f};
    serialization::readVec2(j, "missing_key", out);

    CHECK(out.x == doctest::Approx(7.0f));
    CHECK(out.y == doctest::Approx(8.0f));
}

// ---- writeVec4 / readVec4 ----

TEST_CASE("vec4: writeVec4/readVec4 roundtrip") {
    glm::vec4 original{1.0f, 0.5f, 0.25f, 1.0f};
    nlohmann::json j;
    j["color"] = serialization::writeVec4(original);

    glm::vec4 restored{0.0f, 0.0f, 0.0f, 0.0f};
    serialization::readVec4(j, "color", restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
    CHECK(restored.z == doctest::Approx(original.z));
    CHECK(restored.w == doctest::Approx(original.w));
}

TEST_CASE("vec4: negative and large values") {
    glm::vec4 original{-1e5f, 0.0f, 1e5f, -0.0001f};
    nlohmann::json j;
    j["v"] = serialization::writeVec4(original);

    glm::vec4 restored{0.0f, 0.0f, 0.0f, 0.0f};
    serialization::readVec4(j, "v", restored);

    CHECK(restored.x == doctest::Approx(original.x));
    CHECK(restored.y == doctest::Approx(original.y));
    CHECK(restored.z == doctest::Approx(original.z));
    CHECK(restored.w == doctest::Approx(original.w));
}

TEST_CASE("vec4: missing key leaves output unchanged") {
    nlohmann::json j;

    glm::vec4 out{1.0f, 2.0f, 3.0f, 4.0f};
    serialization::readVec4(j, "absent", out);

    CHECK(out.x == doctest::Approx(1.0f));
    CHECK(out.y == doctest::Approx(2.0f));
    CHECK(out.z == doctest::Approx(3.0f));
    CHECK(out.w == doctest::Approx(4.0f));
}

} // TEST_SUITE
