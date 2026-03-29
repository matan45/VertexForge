#include <doctest.h>
#include <math/MathHelper.hpp>
#include <math/EasingFunctions.hpp>
#include <math/Frustum.hpp>
#include <math/BVH.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// ============================================================
// VK-1085: Math & Spatial unit tests
// ============================================================

TEST_SUITE("MathSpatial") {

// ---- MathHelper ----

TEST_CASE("MathHelper: clamp within range") {
    CHECK(math::clamp(5.0f, 0.0f, 10.0f) == 5.0f);
}

TEST_CASE("MathHelper: clamp below min") {
    CHECK(math::clamp(-1.0f, 0.0f, 10.0f) == 0.0f);
}

TEST_CASE("MathHelper: clamp above max") {
    CHECK(math::clamp(15.0f, 0.0f, 10.0f) == 10.0f);
}

TEST_CASE("MathHelper: clamp at boundaries") {
    CHECK(math::clamp(0.0f, 0.0f, 10.0f) == 0.0f);
    CHECK(math::clamp(10.0f, 0.0f, 10.0f) == 10.0f);
}

TEST_CASE("MathHelper: smoothstep returns 0 at edge0") {
    CHECK(math::smoothstep(0.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("MathHelper: smoothstep returns 1 at edge1") {
    CHECK(math::smoothstep(0.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("MathHelper: smoothstep returns 0.5 at midpoint") {
    CHECK(math::smoothstep(0.0f, 1.0f, 0.5f) == doctest::Approx(0.5f));
}

TEST_CASE("MathHelper: smoothstep clamps below edge0") {
    CHECK(math::smoothstep(0.0f, 1.0f, -1.0f) == doctest::Approx(0.0f));
}

TEST_CASE("MathHelper: smoothstep clamps above edge1") {
    CHECK(math::smoothstep(0.0f, 1.0f, 2.0f) == doctest::Approx(1.0f));
}

TEST_CASE("MathHelper: lerp at boundaries") {
    CHECK(math::lerp(0.0f, 10.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::lerp(0.0f, 10.0f, 1.0f) == doctest::Approx(10.0f));
}

TEST_CASE("MathHelper: lerp at midpoint") {
    CHECK(math::lerp(0.0f, 10.0f, 0.5f) == doctest::Approx(5.0f));
}

// ---- SDF ----

TEST_CASE("SDF: sdfToAlpha at edge center is ~0.5") {
    float alpha = sdf::sdfToAlpha(sdf::DEFAULT_EDGE_VALUE, sdf::DEFAULT_EDGE_VALUE, sdf::DEFAULT_SMOOTH_WIDTH);
    CHECK(alpha == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("SDF: sdfToAlpha inside is ~1.0") {
    float alpha = sdf::sdfToAlpha(255.0f, sdf::DEFAULT_EDGE_VALUE, sdf::DEFAULT_SMOOTH_WIDTH);
    CHECK(alpha == doctest::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("SDF: sdfToAlpha outside is ~0.0") {
    float alpha = sdf::sdfToAlpha(0.0f, sdf::DEFAULT_EDGE_VALUE, sdf::DEFAULT_SMOOTH_WIDTH);
    CHECK(alpha == doctest::Approx(0.0f).epsilon(0.01f));
}

// ---- EasingFunctions ----

TEST_CASE("EasingFunctions: all return 0 at t=0") {
    using namespace components;
    CHECK(math::evaluateEasing(UIEasingFunction::Linear, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseIn, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseOut, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseInOut, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::Bounce, 0.0f) == doctest::Approx(0.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::Elastic, 0.0f) == doctest::Approx(0.0f).epsilon(0.01f));
}

TEST_CASE("EasingFunctions: all return 1 at t=1") {
    using namespace components;
    CHECK(math::evaluateEasing(UIEasingFunction::Linear, 1.0f) == doctest::Approx(1.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseIn, 1.0f) == doctest::Approx(1.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseOut, 1.0f) == doctest::Approx(1.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::EaseInOut, 1.0f) == doctest::Approx(1.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::Bounce, 1.0f) == doctest::Approx(1.0f));
    CHECK(math::evaluateEasing(UIEasingFunction::Elastic, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("EasingFunctions: Linear is monotonic and identity") {
    for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
        CHECK(math::evaluateEasing(components::UIEasingFunction::Linear, t) == doctest::Approx(t));
    }
}

// ---- AABB ----

TEST_CASE("AABB: getCenter") {
    math::AABB box({0, 0, 0}, {10, 10, 10});
    auto center = box.getCenter();
    CHECK(center.x == doctest::Approx(5.0f));
    CHECK(center.y == doctest::Approx(5.0f));
    CHECK(center.z == doctest::Approx(5.0f));
}

TEST_CASE("AABB: getSize") {
    math::AABB box({0, 0, 0}, {10, 20, 30});
    auto size = box.getSize();
    CHECK(size.x == doctest::Approx(10.0f));
    CHECK(size.y == doctest::Approx(20.0f));
    CHECK(size.z == doctest::Approx(30.0f));
}

TEST_CASE("AABB: getVolume") {
    math::AABB box({0, 0, 0}, {2, 3, 4});
    CHECK(box.getVolume() == doctest::Approx(24.0f));
}

TEST_CASE("AABB: isValid") {
    math::AABB valid({0, 0, 0}, {1, 1, 1});
    CHECK(valid.isValid());

    math::AABB invalid({1, 1, 1}, {0, 0, 0});
    CHECK_FALSE(invalid.isValid());
}

TEST_CASE("AABB: expand") {
    math::AABB box({0, 0, 0}, {1, 1, 1});
    box.expand({2, 2, 2});
    CHECK(box.max.x == doctest::Approx(2.0f));
    CHECK(box.max.y == doctest::Approx(2.0f));
    CHECK(box.max.z == doctest::Approx(2.0f));
}

TEST_CASE("AABB: expand with point below min") {
    math::AABB box({0, 0, 0}, {1, 1, 1});
    box.expand({-1, -1, -1});
    CHECK(box.min.x == doctest::Approx(-1.0f));
    CHECK(box.min.y == doctest::Approx(-1.0f));
    CHECK(box.min.z == doctest::Approx(-1.0f));
}

// ---- Ray-AABB Intersection ----

TEST_CASE("Ray-AABB: hit from front") {
    math::AABB box({-1, -1, -1}, {1, 1, 1});
    math::Ray ray({0, 0, -5}, {0, 0, 1});
    auto hit = box.intersectRay(ray);
    CHECK(hit.has_value());
    CHECK(hit.value() == doctest::Approx(4.0f));
}

TEST_CASE("Ray-AABB: miss") {
    math::AABB box({-1, -1, -1}, {1, 1, 1});
    math::Ray ray({5, 5, -5}, {0, 0, 1});
    auto hit = box.intersectRay(ray);
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("Ray-AABB: ray origin inside box") {
    math::AABB box({-1, -1, -1}, {1, 1, 1});
    math::Ray ray({0, 0, 0}, {0, 0, 1});
    auto hit = box.intersectRay(ray);
    CHECK(hit.has_value());
}

// ---- Frustum ----

TEST_CASE("Frustum: default is not initialized") {
    math::Frustum frustum;
    CHECK_FALSE(frustum.isInitialized());
}

TEST_CASE("Frustum: extract from VP matrix initializes") {
    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);
    CHECK(frustum.isInitialized());
}

TEST_CASE("Frustum: AABB inside frustum is detected") {
    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);

    math::AABB box({-1, -1, -5}, {1, 1, -3});
    CHECK(frustum.intersectsAABB(box));
}

TEST_CASE("Frustum: AABB behind camera is culled") {
    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);

    math::AABB box({-1, -1, 5}, {1, 1, 10});
    CHECK_FALSE(frustum.intersectsAABB(box));
}

TEST_CASE("Frustum: sphere inside frustum") {
    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);

    CHECK(frustum.intersectsSphere({0, 0, -5}, 1.0f));
}

TEST_CASE("Frustum: sphere behind camera") {
    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);

    CHECK_FALSE(frustum.intersectsSphere({0, 0, 10}, 1.0f));
}

// ---- BVH ----

TEST_CASE("BVH: default is not built") {
    math::BVH bvh;
    CHECK_FALSE(bvh.isBuilt());
    CHECK(bvh.getNodeCount() == 0);
    CHECK(bvh.getPrimitiveCount() == 0);
}

TEST_CASE("BVH: build with primitives") {
    std::vector<math::BVHPrimitive> prims;
    for (uint32_t i = 0; i < 10; ++i) {
        float x = static_cast<float>(i) * 10.0f;
        prims.push_back({
            math::AABB({x, 0, 0}, {x + 1, 1, 1}),
            i
        });
    }
    math::BVH bvh;
    bvh.build(std::move(prims));
    CHECK(bvh.isBuilt());
    CHECK(bvh.getPrimitiveCount() == 10);
    CHECK(bvh.getNodeCount() > 0);
}

TEST_CASE("BVH: queryFrustum returns correct entities") {
    std::vector<math::BVHPrimitive> prims;
    // Place entities at known positions along -Z axis
    for (uint32_t i = 0; i < 5; ++i) {
        float z = -static_cast<float>(i + 1) * 10.0f;
        prims.push_back({
            math::AABB({-1, -1, z - 1}, {1, 1, z + 1}),
            i
        });
    }
    // Place one entity behind camera
    prims.push_back({
        math::AABB({-1, -1, 5}, {1, 1, 10}),
        99
    });

    math::BVH bvh;
    bvh.build(std::move(prims));

    math::Frustum frustum;
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 200.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    frustum.extractFromMatrix(proj * view);

    std::vector<uint32_t> results;
    bvh.queryFrustum(frustum, results);

    // The 5 entities in front should be found, entity 99 behind should not
    CHECK(results.size() >= 5);
    bool found99 = false;
    for (auto id : results) {
        if (id == 99) found99 = true;
    }
    CHECK_FALSE(found99);
}

TEST_CASE("BVH: hasEntity") {
    std::vector<math::BVHPrimitive> prims;
    prims.push_back({math::AABB({0, 0, 0}, {1, 1, 1}), 42});
    math::BVH bvh;
    bvh.build(std::move(prims));
    CHECK(bvh.hasEntity(42));
    CHECK_FALSE(bvh.hasEntity(99));
}

TEST_CASE("BVH: clear resets state") {
    std::vector<math::BVHPrimitive> prims;
    prims.push_back({math::AABB({0, 0, 0}, {1, 1, 1}), 1});
    math::BVH bvh;
    bvh.build(std::move(prims));
    CHECK(bvh.isBuilt());
    bvh.clear();
    CHECK_FALSE(bvh.isBuilt());
    CHECK(bvh.getPrimitiveCount() == 0);
}

} // TEST_SUITE
