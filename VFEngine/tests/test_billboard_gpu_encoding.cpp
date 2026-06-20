// CPU-side round-trip tests for the GPU billboard flipbook cols/rows encoding.
//
// BillboardInstanceGPU packs sprite-sheet (cols, rows) into a single float
// (floor(cols)*256 + rows) so the mesh shader can decode it without an extra
// field. These tests pin the encode/decode contract and the in-shader decode
// formula (cols = floor(v/256), rows = v - cols*256), which must stay in lockstep
// with resources/shaders/gpudriven/mesh_billboard.glsl.

#include "doctest.h"
#include "render/gpudriven/billboard/BillboardGPUTypes.hpp"

#include <cmath>

using render::gpudriven::encodeFlipbookColsRows;
using render::gpudriven::decodeFlipbookColsRows;

namespace
{
    // Mirror of the GLSL decode in mesh_billboard.glsl, operating on the encoded float.
    void shaderDecode(float encoded, float& colsOut, float& rowsOut)
    {
        colsOut = std::floor(encoded / 256.0f);
        rowsOut = encoded - colsOut * 256.0f;
    }
}

TEST_CASE("billboard flipbook cols/rows encode round-trips")
{
    SUBCASE("identity (no flipbook)")
    {
        uint32_t c = 99, r = 99;
        decodeFlipbookColsRows(encodeFlipbookColsRows(1, 1), c, r);
        CHECK(c == 1u);
        CHECK(r == 1u);
    }

    SUBCASE("typical sprite sheets")
    {
        const std::pair<uint32_t, uint32_t> cases[] = {
            {4, 4}, {8, 8}, {1, 16}, {16, 1}, {5, 3}, {12, 7}};
        for (auto [cols, rows] : cases)
        {
            uint32_t c = 0, r = 0;
            decodeFlipbookColsRows(encodeFlipbookColsRows(cols, rows), c, r);
            CHECK(c == cols);
            CHECK(r == rows);
        }
    }

    SUBCASE("max representable (255,255)")
    {
        uint32_t c = 0, r = 0;
        decodeFlipbookColsRows(encodeFlipbookColsRows(255, 255), c, r);
        CHECK(c == 255u);
        CHECK(r == 255u);
    }

    SUBCASE("values above 255 are clamped")
    {
        uint32_t c = 0, r = 0;
        decodeFlipbookColsRows(encodeFlipbookColsRows(300, 9), c, r);
        CHECK(c == 255u); // clamped
        CHECK(r == 9u);
    }
}

TEST_CASE("billboard flipbook encoding matches the in-shader decode")
{
    // The CPU encode and the GLSL decode must agree exactly so the gathered
    // instance and the rendered frame select the same tile.
    const std::pair<uint32_t, uint32_t> cases[] = {
        {1, 1}, {4, 4}, {8, 2}, {3, 7}, {255, 255}};

    for (auto [cols, rows] : cases)
    {
        const float encoded = encodeFlipbookColsRows(cols, rows);
        float shaderCols = 0.0f, shaderRows = 0.0f;
        shaderDecode(encoded, shaderCols, shaderRows);
        CHECK(static_cast<uint32_t>(shaderCols) == cols);
        CHECK(static_cast<uint32_t>(shaderRows) == rows);
    }
}

TEST_CASE("BillboardInstanceGPU stays 80 bytes (std430 mirror)")
{
    // The shader BillboardInstance struct is byte-matched to this layout; any size
    // drift silently corrupts every field after the change.
    CHECK(sizeof(render::gpudriven::BillboardInstanceGPU) == 80);
}
