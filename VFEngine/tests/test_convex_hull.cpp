#include <doctest.h>
#include <resource/ConvexHullTypes.hpp>

// ============================================================
// ConvexHullTypes tests - decomposition data and parameters
// ============================================================

TEST_SUITE("ConvexHullTypes") {

// ---- ConvexDecompositionData::isValid ----

TEST_CASE("ConvexDecompositionData: default is not valid") {
    resource::ConvexDecompositionData data;
    CHECK_FALSE(data.isValid());
    CHECK_FALSE(data.hasDecomposition);
    CHECK(data.hulls.empty());
}

TEST_CASE("ConvexDecompositionData: hasDecomposition true but empty hulls is not valid") {
    resource::ConvexDecompositionData data;
    data.hasDecomposition = true;
    CHECK_FALSE(data.isValid());
}

TEST_CASE("ConvexDecompositionData: hasDecomposition true with hull is valid") {
    resource::ConvexDecompositionData data;
    data.hasDecomposition = true;

    resource::ConvexHull hull;
    hull.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    hull.indices = {0, 1, 2};
    hull.center = {0.33f, 0.33f, 0.0f};
    hull.volume = 0.5f;
    data.hulls.push_back(hull);

    CHECK(data.isValid());
}

TEST_CASE("ConvexDecompositionData: hasDecomposition false with hulls is not valid") {
    resource::ConvexDecompositionData data;
    data.hasDecomposition = false;

    resource::ConvexHull hull;
    hull.vertices = {{0.0f, 0.0f, 0.0f}};
    data.hulls.push_back(hull);

    CHECK_FALSE(data.isValid());
}

// ---- getTotalVertexCount ----

TEST_CASE("getTotalVertexCount: empty data returns 0") {
    resource::ConvexDecompositionData data;
    CHECK(data.getTotalVertexCount() == 0);
}

TEST_CASE("getTotalVertexCount: single hull") {
    resource::ConvexDecompositionData data;
    resource::ConvexHull hull;
    hull.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    data.hulls.push_back(hull);

    CHECK(data.getTotalVertexCount() == 3);
}

TEST_CASE("getTotalVertexCount: multiple hulls sums correctly") {
    resource::ConvexDecompositionData data;

    resource::ConvexHull hull1;
    hull1.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};

    resource::ConvexHull hull2;
    hull2.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                      {0.0f, 0.0f, 1.0f}};

    resource::ConvexHull hull3;
    // empty hull
    data.hulls.push_back(hull1);
    data.hulls.push_back(hull2);
    data.hulls.push_back(hull3);

    CHECK(data.getTotalVertexCount() == 6); // 2 + 4 + 0
}

// ---- ConvexDecompositionParams defaults ----

TEST_CASE("ConvexDecompositionParams: defaults are reasonable") {
    resource::ConvexDecompositionParams params;
    CHECK(params.maxConvexHulls > 0);
    CHECK(params.resolution > 0);
    CHECK(params.maxVerticesPerHull > 0);
    CHECK(params.maxRecursionDepth > 0);
    CHECK(params.minVolumePercentError > 0.0f);
    CHECK(params.shrinkWrap == true);
}

TEST_CASE("ConvexDecompositionParams: specific default values") {
    resource::ConvexDecompositionParams params;
    CHECK(params.maxConvexHulls == 16);
    CHECK(params.resolution == 100000);
    CHECK(params.maxVerticesPerHull == 32);
    CHECK(params.maxRecursionDepth == 10);
    CHECK(params.minVolumePercentError == doctest::Approx(1.0f));
}

// ---- ConvexHull defaults ----

TEST_CASE("ConvexHull: default values") {
    resource::ConvexHull hull;
    CHECK(hull.vertices.empty());
    CHECK(hull.indices.empty());
    CHECK(hull.center.x == doctest::Approx(0.0f));
    CHECK(hull.center.y == doctest::Approx(0.0f));
    CHECK(hull.center.z == doctest::Approx(0.0f));
    CHECK(hull.volume == doctest::Approx(0.0f));
}

} // TEST_SUITE
