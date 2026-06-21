#include <doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <optional>
#include <vector>

// ============================================================
// VK-1416 / VK-1417: Camera matrix scripting round-trip.
//
// The engine exposes two pure-math helpers in
//   VFEngine/core/adapters/api/NativeHelpers.hpp
// that bridge a glm::mat4 (column-major: m[col][row]) to/from the
// 16-float ROW-MAJOR layout that mType scripts see as a Matrix4f
// (value::NativeArray):
//
//   makeMat4Array(m):  arr[row*4 + col] = m[col][row]   // glm -> row-major flat
//   extractMat4(arr):  out[col][row]    = arr[row*4 + col] // row-major flat -> glm
//
// extractMat4 returns false unless the value is a >=16-element array.
//
// Those helpers can NOT be #included here: they pull in mType's
// value::Value / NativeArray (the NativeArray-backed Value ctor is a
// non-inline mType .cpp symbol), and the Tests project deliberately
// neither adds dependencies/mType/mType to its include path nor links
// mType.lib. Linking the whole interpreter into the CPU test runner for
// one math helper is not warranted.
//
// Instead we replicate the EXACT row/col transpose contract below and
// pin it against glm. If the production indexing in NativeHelpers.hpp
// ever changes, these mirrors must change with it (and vice versa) —
// that is the invariant this file protects.
// ============================================================

namespace
{
    // Mirror of core::api::makeMat4Array: glm (column-major) -> 16 floats row-major.
    std::vector<float> makeMat4Array(const glm::mat4& m)
    {
        std::vector<float> arr(16, 0.0f);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                arr[row * 4 + col] = m[col][row];
        return arr;
    }

    // Mirror of core::api::extractMat4: 16-float row-major -> glm (column-major).
    // Returns std::nullopt when the array is shorter than 16 (stands in for the
    // "not a >=16-element NativeArray" false return of the real helper).
    std::optional<glm::mat4> extractMat4(const std::vector<float>& arr)
    {
        if (arr.size() < 16) return std::nullopt;
        glm::mat4 out(0.0f);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                out[col][row] = arr[row * 4 + col];
        return out;
    }

    void checkMatricesEqual(const glm::mat4& a, const glm::mat4& b)
    {
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                CHECK(a[col][row] == doctest::Approx(b[col][row]));
    }
}

TEST_SUITE("CameraMatrix")
{
    TEST_CASE("makeMat4Array emits row-major order")
    {
        // Build a matrix whose every element is unique and decodable so the
        // exact flat layout is observable: m[col][row] = row*4 + col + 1.
        // Row-major flat index r*4+c must then hold (r*4 + c + 1).
        glm::mat4 m(0.0f);
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                m[col][row] = static_cast<float>(row * 4 + col + 1);

        auto arr = makeMat4Array(m);
        REQUIRE(arr.size() == 16);
        for (int i = 0; i < 16; ++i)
            CHECK(arr[i] == doctest::Approx(static_cast<float>(i + 1)));
    }

    TEST_CASE("round-trip: identity")
    {
        glm::mat4 m(1.0f);
        auto rt = extractMat4(makeMat4Array(m));
        REQUIRE(rt.has_value());
        checkMatricesEqual(*rt, m);
    }

    TEST_CASE("round-trip: glm::lookAt")
    {
        glm::mat4 m = glm::lookAt(glm::vec3(3.0f, 4.0f, 5.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f),
                                  glm::vec3(0.0f, 1.0f, 0.0f));
        auto rt = extractMat4(makeMat4Array(m));
        REQUIRE(rt.has_value());
        checkMatricesEqual(*rt, m);
    }

    TEST_CASE("round-trip: glm::perspective")
    {
        glm::mat4 m = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
        auto rt = extractMat4(makeMat4Array(m));
        REQUIRE(rt.has_value());
        checkMatricesEqual(*rt, m);
    }

    TEST_CASE("round-trip: arbitrary asymmetric matrix")
    {
        // No symmetry across the diagonal so a stray transpose would be caught.
        glm::mat4 m(0.0f);
        float v = 0.5f;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
            {
                m[col][row] = v;
                v += 1.25f;
            }
        auto rt = extractMat4(makeMat4Array(m));
        REQUIRE(rt.has_value());
        checkMatricesEqual(*rt, m);
    }

    TEST_CASE("extractMat4 rejects short array")
    {
        std::vector<float> shortArr(15, 1.0f);
        CHECK_FALSE(extractMat4(shortArr).has_value());

        std::vector<float> empty;
        CHECK_FALSE(extractMat4(empty).has_value());
    }

    TEST_CASE("extractMat4 accepts exactly 16 and ignores trailing extras")
    {
        // The real helper checks size() >= 16 and only reads the first 16.
        std::vector<float> arr(16, 0.0f);
        for (int i = 0; i < 16; ++i) arr[i] = static_cast<float>(i);
        CHECK(extractMat4(arr).has_value());

        arr.push_back(999.0f); // 17 elements
        auto rt = extractMat4(arr);
        REQUIRE(rt.has_value());
        // Element [3][3] is flat index 15; the 17th element (index 16) is unread.
        CHECK((*rt)[3][3] == doctest::Approx(15.0f));
    }
}
