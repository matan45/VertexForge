#include <doctest.h>
#include <resource/VertexQuantization.hpp>
#include <resource/Types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// ============================================================
// VK-1060: Vertex quantization unit tests
// ============================================================

using namespace resource;
using namespace resource::quantization;

static Vertex makeVertex(glm::vec3 pos, glm::vec3 nrm, glm::vec2 uv)
{
    Vertex v{};
    v.position = pos;
    v.normal = nrm;
    v.texCoords = uv;
    return v;
}

TEST_SUITE("VertexQuantization") {

// ---- computeAABB ----

TEST_CASE("computeAABB: correct min/max from known vertices") {
    std::vector<Vertex> verts = {
        makeVertex({1.0f, -2.0f, 3.0f}, {0, 1, 0}, {0, 0}),
        makeVertex({-5.0f, 4.0f, 0.5f}, {0, 1, 0}, {0, 0}),
        makeVertex({2.0f, 0.0f, -1.0f}, {0, 1, 0}, {0, 0}),
    };

    auto aabb = computeAABB(verts);

    CHECK(aabb.min.x == doctest::Approx(-5.0f));
    CHECK(aabb.min.y == doctest::Approx(-2.0f));
    CHECK(aabb.min.z == doctest::Approx(-1.0f));
    CHECK(aabb.max.x == doctest::Approx(2.0f));
    CHECK(aabb.max.y == doctest::Approx(4.0f));
    CHECK(aabb.max.z == doctest::Approx(3.0f));
}

TEST_CASE("computeAABB: single vertex gives identical min and max") {
    std::vector<Vertex> verts = {
        makeVertex({7.0f, -3.0f, 1.0f}, {0, 1, 0}, {0, 0}),
    };

    auto aabb = computeAABB(verts);

    CHECK(aabb.min.x == doctest::Approx(7.0f));
    CHECK(aabb.min.y == doctest::Approx(-3.0f));
    CHECK(aabb.min.z == doctest::Approx(1.0f));
    CHECK(aabb.max.x == doctest::Approx(7.0f));
    CHECK(aabb.max.y == doctest::Approx(-3.0f));
    CHECK(aabb.max.z == doctest::Approx(1.0f));
}

TEST_CASE("computeAABB: empty vertices returns zeroed AABB") {
    std::vector<Vertex> verts;
    auto aabb = computeAABB(verts);

    CHECK(aabb.min.x == doctest::Approx(0.0f));
    CHECK(aabb.min.y == doctest::Approx(0.0f));
    CHECK(aabb.min.z == doctest::Approx(0.0f));
    CHECK(aabb.max.x == doctest::Approx(0.0f));
    CHECK(aabb.max.y == doctest::Approx(0.0f));
    CHECK(aabb.max.z == doctest::Approx(0.0f));
}

// ---- octEncode / octDecode roundtrip ----

TEST_CASE("octEncode/octDecode roundtrip preserves unit normals") {
    struct NormalCase {
        glm::vec3 normal;
        const char* label;
    };

    NormalCase cases[] = {
        {{0.0f, 1.0f, 0.0f}, "+Y up"},
        {{1.0f, 0.0f, 0.0f}, "+X right"},
        {{0.0f, 0.0f, 1.0f}, "+Z forward"},
        {{0.0f, -1.0f, 0.0f}, "-Y down"},
        {{-1.0f, 0.0f, 0.0f}, "-X left"},
        {{0.0f, 0.0f, -1.0f}, "-Z back"},
    };

    for (const auto& tc : cases) {
        CAPTURE(tc.label);
        int16_t ex, ey;
        octEncode(tc.normal, ex, ey);
        glm::vec3 decoded = octDecode(ex, ey);
        float dp = glm::dot(decoded, tc.normal);
        CHECK(dp > 0.99f);
    }
}

TEST_CASE("octEncode/octDecode roundtrip for arbitrary normalized vector") {
    glm::vec3 n = glm::normalize(glm::vec3(0.3f, -0.7f, 0.5f));
    int16_t ex, ey;
    octEncode(n, ex, ey);
    glm::vec3 decoded = octDecode(ex, ey);
    float dp = glm::dot(decoded, n);
    CHECK(dp > 0.99f);
}

// ---- quantizeVertex / dequantizeVertex roundtrip ----

TEST_CASE("quantizeVertex/dequantizeVertex roundtrip preserves data") {
    Vertex v{};
    v.position = {1.5f, 2.5f, 3.5f};
    v.normal = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
    v.texCoords = {0.25f, 0.75f};
    v.boneIndices = {0, 1, -1, -1};
    v.boneWeights = {0.6f, 0.4f, 0.0f, 0.0f};

    QuantizationAABB aabb;
    aabb.min = {0.0f, 0.0f, 0.0f};
    aabb.max = {10.0f, 10.0f, 10.0f};

    CompressedVertex cv = quantizeVertex(v, aabb);
    Vertex restored = dequantizeVertex(cv, aabb);

    // Position error should be small relative to AABB extent (10.0)
    // 16-bit quantization: max error ~= extent / 65535
    float maxPosError = 10.0f / 65535.0f * 2.0f; // small margin
    CHECK(std::abs(restored.position.x - v.position.x) < maxPosError);
    CHECK(std::abs(restored.position.y - v.position.y) < maxPosError);
    CHECK(std::abs(restored.position.z - v.position.z) < maxPosError);

    // Normal preserved
    float dp = glm::dot(restored.normal, v.normal);
    CHECK(dp > 0.95f);

    // UVs preserved approximately (float16 precision)
    CHECK(restored.texCoords.x == doctest::Approx(v.texCoords.x).epsilon(0.01));
    CHECK(restored.texCoords.y == doctest::Approx(v.texCoords.y).epsilon(0.01));

    // Bone indices preserved
    CHECK(restored.boneIndices.x == 0);
    CHECK(restored.boneIndices.y == 1);
    CHECK(restored.boneIndices.z == -1);
    CHECK(restored.boneIndices.w == -1);
}

TEST_CASE("quantizeVertex/dequantizeVertex position at AABB boundaries") {
    QuantizationAABB aabb;
    aabb.min = {-5.0f, -5.0f, -5.0f};
    aabb.max = {5.0f, 5.0f, 5.0f};
    float extent = 10.0f;
    float maxPosError = extent / 65535.0f * 2.0f;

    SUBCASE("position at min corner") {
        Vertex v = makeVertex(aabb.min, {0, 1, 0}, {0, 0});
        Vertex restored = dequantizeVertex(quantizeVertex(v, aabb), aabb);
        CHECK(std::abs(restored.position.x - aabb.min.x) < maxPosError);
        CHECK(std::abs(restored.position.y - aabb.min.y) < maxPosError);
        CHECK(std::abs(restored.position.z - aabb.min.z) < maxPosError);
    }

    SUBCASE("position at max corner") {
        Vertex v = makeVertex(aabb.max, {0, 1, 0}, {0, 0});
        Vertex restored = dequantizeVertex(quantizeVertex(v, aabb), aabb);
        CHECK(std::abs(restored.position.x - aabb.max.x) < maxPosError);
        CHECK(std::abs(restored.position.y - aabb.max.y) < maxPosError);
        CHECK(std::abs(restored.position.z - aabb.max.z) < maxPosError);
    }
}

// ---- Batch quantizeVertices ----

TEST_CASE("quantizeVertices produces same count as input") {
    std::vector<Vertex> verts = {
        makeVertex({0, 0, 0}, {0, 1, 0}, {0, 0}),
        makeVertex({1, 1, 1}, {1, 0, 0}, {1, 1}),
        makeVertex({2, 3, 4}, {0, 0, 1}, {0.5f, 0.5f}),
    };

    auto aabb = computeAABB(verts);
    auto compressed = quantizeVertices(verts, aabb);

    CHECK(compressed.size() == verts.size());
}

TEST_CASE("quantizeVertices: empty input produces empty output") {
    std::vector<Vertex> verts;
    QuantizationAABB aabb;
    auto compressed = quantizeVertices(verts, aabb);
    CHECK(compressed.empty());
}

// ---- Edge case: zero-extent AABB (flat plane) ----

TEST_CASE("quantize/dequantize handles zero-extent AABB (flat plane)") {
    // All vertices on the same Y plane
    std::vector<Vertex> verts = {
        makeVertex({0.0f, 5.0f, 0.0f}, {0, 1, 0}, {0, 0}),
        makeVertex({3.0f, 5.0f, 2.0f}, {0, 1, 0}, {1, 1}),
        makeVertex({1.0f, 5.0f, 4.0f}, {0, 1, 0}, {0.5f, 0.5f}),
    };

    auto aabb = computeAABB(verts);
    // Y extent is zero
    CHECK(aabb.min.y == doctest::Approx(aabb.max.y));

    // Should not crash or produce NaN
    auto compressed = quantizeVertices(verts, aabb);
    CHECK(compressed.size() == 3);

    for (size_t i = 0; i < verts.size(); ++i) {
        Vertex restored = dequantizeVertex(compressed[i], aabb);
        CHECK_FALSE(std::isnan(restored.position.x));
        CHECK_FALSE(std::isnan(restored.position.y));
        CHECK_FALSE(std::isnan(restored.position.z));

        // X and Z should still be close
        float xExtent = aabb.max.x - aabb.min.x;
        float zExtent = aabb.max.z - aabb.min.z;
        float maxXError = (xExtent > 0.0f ? xExtent : 1.0f) / 65535.0f * 2.0f;
        float maxZError = (zExtent > 0.0f ? zExtent : 1.0f) / 65535.0f * 2.0f;
        CHECK(std::abs(restored.position.x - verts[i].position.x) < maxXError);
        CHECK(std::abs(restored.position.z - verts[i].position.z) < maxZError);
    }
}

} // TEST_SUITE
