#include <doctest.h>
#include <foliage/FoliageNavGeometry.hpp>
#include <foliage/FoliageTypes.hpp>
#include <navigation/NavmeshData.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <cstdint>

// ============================================================
// VK-1584: foliage -> navmesh geometry transform-and-append (pure).
// Exercises the per-instance TRS transform + base-vertex offsetting used by
// NavmeshServiceImpl::collectFoliageGeometryForBounds — with a stub mesh, no
// Recast, no ResourceManager, no scene.
// ============================================================

namespace
{
    // A minimal single-triangle stub mesh in mesh-local space.
    const std::vector<glm::vec3> kStubPositions = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
    };
    const std::vector<uint32_t> kStubIndices = {0u, 1u, 2u};

    glm::vec3 vertAt(const navigation::NavmeshInputGeometry& g, int i)
    {
        return glm::vec3(g.vertices[i * 3], g.vertices[i * 3 + 1], g.vertices[i * 3 + 2]);
    }
}

TEST_SUITE("FoliageNavGeometry")
{

TEST_CASE("identity instance appends the mesh verbatim")
{
    foliage::FoliageInstance fi; // identity transform
    foliage::FoliageType type;   // alignToNormal defaults false
    navigation::NavmeshInputGeometry out;

    foliage::appendFoliageInstanceGeometry(fi, type, kStubPositions, kStubIndices, out);

    REQUIRE(out.getVertexCount() == 3);
    REQUIRE(out.getTriangleCount() == 1);
    CHECK(vertAt(out, 1).x == doctest::Approx(1.0f));
    CHECK(vertAt(out, 2).z == doctest::Approx(1.0f));
    CHECK(out.triangles[0] == 0);
    CHECK(out.triangles[1] == 1);
    CHECK(out.triangles[2] == 2);
}

TEST_CASE("translation offsets every appended vertex")
{
    foliage::FoliageInstance fi;
    fi.position = glm::vec3(10.0f, 2.0f, 5.0f);
    foliage::FoliageType type;
    navigation::NavmeshInputGeometry out;

    foliage::appendFoliageInstanceGeometry(fi, type, kStubPositions, kStubIndices, out);

    CHECK(vertAt(out, 0).x == doctest::Approx(10.0f)); // (0,0,0) + pos
    CHECK(vertAt(out, 0).y == doctest::Approx(2.0f));
    CHECK(vertAt(out, 0).z == doctest::Approx(5.0f));
    CHECK(vertAt(out, 1).x == doctest::Approx(11.0f)); // (1,0,0) + pos
}

TEST_CASE("non-uniform scale is applied before translation")
{
    foliage::FoliageInstance fi;
    fi.scale = glm::vec3(2.0f, 1.0f, 3.0f);
    foliage::FoliageType type;
    navigation::NavmeshInputGeometry out;

    foliage::appendFoliageInstanceGeometry(fi, type, kStubPositions, kStubIndices, out);

    CHECK(vertAt(out, 1).x == doctest::Approx(2.0f)); // (1,0,0) * scale.x
    CHECK(vertAt(out, 2).z == doctest::Approx(3.0f)); // (0,0,1) * scale.z
}

TEST_CASE("rotationY = pi flips the X and Z of local vertices")
{
    foliage::FoliageInstance fi;
    fi.rotationY = glm::pi<float>();
    foliage::FoliageType type;
    navigation::NavmeshInputGeometry out;

    foliage::appendFoliageInstanceGeometry(fi, type, kStubPositions, kStubIndices, out);

    CHECK(vertAt(out, 1).x == doctest::Approx(-1.0f).epsilon(0.0001)); // (1,0,0) -> (-1,0,0)
    CHECK(vertAt(out, 2).z == doctest::Approx(-1.0f).epsilon(0.0001)); // (0,0,1) -> (0,0,-1)
}

TEST_CASE("a second instance's triangle indices are offset by the running vertex count")
{
    foliage::FoliageInstance a; // at origin
    foliage::FoliageInstance b;
    b.position = glm::vec3(10.0f, 0.0f, 0.0f);
    foliage::FoliageType type;
    navigation::NavmeshInputGeometry out;

    foliage::appendFoliageInstanceGeometry(a, type, kStubPositions, kStubIndices, out);
    foliage::appendFoliageInstanceGeometry(b, type, kStubPositions, kStubIndices, out);

    REQUIRE(out.getVertexCount() == 6);
    REQUIRE(out.getTriangleCount() == 2);
    CHECK(out.triangles[0] == 0); // first triangle
    CHECK(out.triangles[3] == 3); // second triangle base-offset by 3
    CHECK(out.triangles[4] == 4);
    CHECK(out.triangles[5] == 5);
    CHECK(vertAt(out, 3).x == doctest::Approx(10.0f)); // second instance translated
}

TEST_CASE("appendTransformedMesh ignores trailing indices that do not complete a triangle")
{
    navigation::NavmeshInputGeometry out;
    std::vector<uint32_t> ragged = {0u, 1u, 2u, 0u}; // 4 indices -> exactly 1 triangle
    foliage::appendTransformedMesh(glm::mat4(1.0f), kStubPositions, ragged, out);
    CHECK(out.getTriangleCount() == 1);
}

} // TEST_SUITE
