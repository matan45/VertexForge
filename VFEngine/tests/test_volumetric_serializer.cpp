#include <doctest.h>
#include <navigation/volumetric/VolumetricSerializer.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================
// VK-1660: the .vfVolNav format (volumetric::encodeVolume / decodeVolume) - a lossless round trip and the
// validation of every header field and of the run-length payload.
// Header offsets (VolumetricSerializer.hpp field order, little-endian): 0 magic, 4 version, 8 origin,
// 20 voxelSize, 24 dims, 36 clearance, 40 connectivity, 44 navigable count, 52 payload size, 60 payload.
// ============================================================

namespace
{
    namespace vol = volumetric;

    vol::VoxelGrid patternGrid()
    {
        vol::VolumetricBakeSettings settings;
        settings.voxelSize = 0.5f;
        settings.clearance = 0.25f;
        settings.connectivity = 6;
        vol::VoxelGrid grid(glm::vec3(-3.0f, 1.5f, 7.0f), glm::ivec3(5, 4, 3), settings);
        for (int z = 0; z < 3; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 5; ++x)
                    grid.setNavigable(vol::VoxelCoord{x, y, z}, (x + 2 * y + 3 * z) % 4 != 0);
        return grid;
    }

    void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            bytes[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }

    void writeU64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            bytes[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }

    bool refused(const std::vector<uint8_t>& bytes, const std::string& expected)
    {
        std::string error;
        const auto decoded = vol::decodeVolume(bytes, error);
        INFO("error: ", error);
        return !decoded && error.find(expected) != std::string::npos;
    }
}

TEST_SUITE("VolumetricSerializer")
{
    TEST_CASE("encode / decode reproduces the grid and its settings")
    {
        const vol::VoxelGrid grid = patternGrid();
        const std::vector<uint8_t> bytes = vol::encodeVolume(grid);
        REQUIRE(bytes.size() > 60);
        CHECK(bytes[0] == 'V');
        CHECK(bytes[3] == 'O');

        std::string error;
        const auto decoded = vol::decodeVolume(bytes, error);
        REQUIRE(decoded);
        CHECK(error.empty());
        CHECK(decoded->origin() == grid.origin());
        CHECK(decoded->dims() == grid.dims());
        CHECK(decoded->settings() == grid.settings());
        CHECK(decoded->data() == grid.data());
        CHECK(vol::encodeVolume(*decoded) == bytes); // stable
    }

    TEST_CASE("all-navigable and all-blocked grids round-trip")
    {
        vol::VoxelGrid blocked(glm::vec3(0.0f), glm::ivec3(4), vol::VolumetricBakeSettings{});
        vol::VoxelGrid open(glm::vec3(0.0f), glm::ivec3(4), vol::VolumetricBakeSettings{});
        for (auto& cell : open.data())
            cell = 1;

        for (const vol::VoxelGrid* grid : {&blocked, &open})
        {
            std::string error;
            const auto decoded = vol::decodeVolume(vol::encodeVolume(*grid), error);
            REQUIRE(decoded);
            CHECK(decoded->data() == grid->data());
        }
    }

    TEST_CASE("a damaged header is refused")
    {
        const std::vector<uint8_t> good = vol::encodeVolume(patternGrid());

        auto badMagic = good;
        badMagic[0] = 'X';
        CHECK(refused(badMagic, "bad magic"));

        auto badVersion = good;
        writeU32(badVersion, 4, 3);
        CHECK(refused(badVersion, "version 3"));

        auto badConnectivity = good;
        writeU32(badConnectivity, 40, 8);
        CHECK(refused(badConnectivity, "connectivity"));

        auto zeroDims = good;
        writeU32(zeroDims, 24, 0);
        CHECK(refused(zeroDims, "not positive"));

        CHECK(refused(std::vector<uint8_t>(good.begin(), good.begin() + 30), "too short"));
    }

    TEST_CASE("dimensions above the cap are refused before anything is allocated")
    {
        auto huge = vol::encodeVolume(patternGrid());
        writeU32(huge, 24, 4096);
        writeU32(huge, 28, 4096);
        writeU32(huge, 32, 4096);
        CHECK(refused(huge, "exceed the limit"));
    }

    TEST_CASE("a damaged payload is refused")
    {
        // A 2x2x2 all-navigable grid: payload = varint 0 (blocked run) + varint 8 (navigable run).
        vol::VoxelGrid open(glm::vec3(0.0f), glm::ivec3(2), vol::VolumetricBakeSettings{});
        for (auto& cell : open.data())
            cell = 1;
        const std::vector<uint8_t> good = vol::encodeVolume(open);
        REQUIRE(good.size() == 62);
        REQUIRE(good[60] == 0);
        REQUIRE(good[61] == 8);

        auto shortRuns = good;
        shortRuns[61] = 7;
        CHECK(refused(shortRuns, "the runs cover 7 voxels"));

        auto longRuns = good;
        longRuns[61] = 9;
        CHECK(refused(longRuns, "overruns"));

        auto truncated = good;
        truncated.pop_back();
        CHECK(refused(truncated, "payload"));

        auto wrongCount = good;
        writeU64(wrongCount, 44, 5);
        CHECK(refused(wrongCount, "navigable"));

        auto unterminated = good;
        unterminated[61] = 0x88; // a continuation bit with nothing after it
        CHECK(refused(unterminated, "truncated or malformed"));
    }
}
