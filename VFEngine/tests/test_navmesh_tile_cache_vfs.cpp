#include <doctest.h>
#include <navigation/NavmeshTileCache.hpp>
#include <navigation/NavmeshData.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// NavmeshTileCache disk format + injectable read path (the same
// seam the VirtualFileSystem uses to serve tiles from .vfpak in
// shipped builds): index v6 round-trip incl. streaming block,
// v5 back-compat, in-memory reads, missing tiles
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    struct TempDir
    {
        fs::path path;

        explicit TempDir(const char* name)
            : path(fs::temp_directory_path() / name)
        {
            fs::remove_all(path);
            fs::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }

        std::string str() const { return path.string(); }
    };

    navigation::NavmeshTileData makeTile(int x, int z, uint8_t fill)
    {
        navigation::NavmeshTileData tile;
        tile.x = x;
        tile.y = z;
        tile.data = {fill, fill, fill, fill};
        tile.dataSize = static_cast<uint32_t>(tile.data.size());
        return tile;
    }

    // Slurps every file under dir into a path->bytes map (normalized to '/')
    std::unordered_map<std::string, std::vector<uint8_t>> slurpDirectory(const fs::path& dir)
    {
        std::unordered_map<std::string, std::vector<uint8_t>> files;
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            std::ifstream in(entry.path(), std::ios::binary | std::ios::ate);
            std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
            in.seekg(0);
            in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

            std::string key = entry.path().string();
            for (char& c : key)
                if (c == '\\') c = '/';
            files[key] = std::move(bytes);
        }
        return files;
    }

    void useInMemoryFiles(navigation::NavmeshTileCache& cache,
                          const std::unordered_map<std::string, std::vector<uint8_t>>& files)
    {
        auto normalize = [](std::string path)
        {
            for (char& c : path)
                if (c == '\\') c = '/';
            return path;
        };
        cache.setFileAccess(
            [&files, normalize](const std::string& path) -> std::vector<uint8_t>
            {
                auto it = files.find(normalize(path));
                return it != files.end() ? it->second : std::vector<uint8_t>{};
            },
            [&files, normalize](const std::string& path)
            {
                return files.count(normalize(path)) > 0;
            });
    }
}

TEST_SUITE("NavmeshTileCacheVFS")
{
    TEST_CASE("index v6 round-trips the streaming block")
    {
        TempDir dir("vf_test_navcache_roundtrip");

        navigation::NavmeshTileIndex index;
        index.streaming.enabled = 1;
        index.streaming.loadRadius = 320.0f;
        index.streaming.unloadRadius = 400.0f;
        index.streaming.maxLoadsPerFrame = 5;
        index.streaming.maxUnloadsPerFrame = 3;
        index.streaming.lodDistances[0] = 100.0f;
        index.streaming.lodDistances[1] = 200.0f;
        index.streaming.lodDistances[2] = 300.0f;
        index.tileCoords = {{0, 0}, {1, 0}, {-2, 3}};

        {
            navigation::NavmeshTileCache cache(dir.str());
            CHECK(cache.saveIndex(index));
        }

        navigation::NavmeshTileCache cache(dir.str());
        navigation::NavmeshTileIndex loaded;
        REQUIRE(cache.loadIndex(loaded));

        CHECK(loaded.version == navigation::NAVMESH_TILE_FILE_VERSION);
        CHECK(loaded.streaming.enabled == 1);
        CHECK(loaded.streaming.loadRadius == doctest::Approx(320.0f));
        CHECK(loaded.streaming.unloadRadius == doctest::Approx(400.0f));
        CHECK(loaded.streaming.maxLoadsPerFrame == 5);
        CHECK(loaded.streaming.maxUnloadsPerFrame == 3);
        CHECK(loaded.streaming.lodDistances[2] == doctest::Approx(300.0f));
        REQUIRE(loaded.tileCoords.size() == 3);
        CHECK(loaded.tileCoords[2].x == -2);
        CHECK(loaded.tileCoords[2].z == 3);
    }

    TEST_CASE("tiles and index resolve through an injected reader (pak-style)")
    {
        TempDir dir("vf_test_navcache_inmem");

        navigation::NavmeshTileIndex index;
        index.streaming.enabled = 1;
        index.tileCoords = {{0, 0}, {1, 2}};

        {
            navigation::NavmeshTileCache writer(dir.str());
            CHECK(writer.saveTile(navigation::NavmeshTileCoord{0, 0}, makeTile(0, 0, 0xAB)));
            CHECK(writer.saveTile(navigation::NavmeshTileCoord{1, 2}, makeTile(1, 2, 0xCD)));
            CHECK(writer.saveIndex(index));
        }

        // Move everything into memory and delete the directory — reads must now
        // come exclusively from the injected functions
        auto files = slurpDirectory(dir.path);
        fs::remove_all(dir.path);
        REQUIRE(files.size() == 3);

        navigation::NavmeshTileCache cache(dir.str());
        useInMemoryFiles(cache, files);

        navigation::NavmeshTileIndex loaded;
        REQUIRE(cache.loadIndex(loaded));
        CHECK(loaded.tileCoords.size() == 2);

        navigation::NavmeshTileData tile;
        REQUIRE(cache.loadTile(navigation::NavmeshTileCoord{1, 2}, tile));
        CHECK(tile.x == 1);
        CHECK(tile.y == 2);
        REQUIRE(tile.dataSize == 4);
        CHECK(tile.data[0] == 0xCD);

        CHECK(cache.hasTile(navigation::NavmeshTileCoord{0, 0}));
        CHECK_FALSE(cache.hasTile(navigation::NavmeshTileCoord{9, 9}));
        CHECK_FALSE(cache.loadTile(navigation::NavmeshTileCoord{9, 9}, tile));
    }

    TEST_CASE("version 5 index loads with streaming defaults (off)")
    {
        // Hand-build a v5 index buffer: magic, version, settings, bounds, count, coords
        std::vector<uint8_t> bytes;
        auto append = [&bytes](const void* src, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(src);
            bytes.insert(bytes.end(), p, p + size);
        };

        uint32_t magic = navigation::NAVMESH_FILE_MAGIC;
        uint32_t version = 5;
        types::NavmeshBakeSettings settings;
        glm::vec3 boundsMin{0.0f, -1000.0f, 0.0f};
        glm::vec3 boundsMax{64.0f, 1000.0f, 64.0f};
        uint32_t count = 1;
        int32_t coordX = 0, coordZ = 1;

        append(&magic, sizeof(magic));
        append(&version, sizeof(version));
        append(&settings, sizeof(settings));
        append(&boundsMin, sizeof(boundsMin));
        append(&boundsMax, sizeof(boundsMax));
        append(&count, sizeof(count));
        append(&coordX, sizeof(coordX));
        append(&coordZ, sizeof(coordZ));

        std::unordered_map<std::string, std::vector<uint8_t>> files;
        files["legacy/index.vfNavIndex"] = bytes;

        navigation::NavmeshTileCache cache("legacy");
        useInMemoryFiles(cache, files);

        navigation::NavmeshTileIndex loaded;
        REQUIRE(cache.loadIndex(loaded));
        CHECK(loaded.version == 5);
        CHECK(loaded.streaming.enabled == 0); // default: streaming OFF
        REQUIRE(loaded.tileCoords.size() == 1);
        CHECK(loaded.tileCoords[0].z == 1);
    }

    TEST_CASE("unsupported versions and bad magic are rejected")
    {
        std::unordered_map<std::string, std::vector<uint8_t>> files;

        uint32_t badMagic = 0x12345678;
        uint32_t version = navigation::NAVMESH_TILE_FILE_VERSION;
        std::vector<uint8_t> bad(sizeof(badMagic) + sizeof(version));
        std::memcpy(bad.data(), &badMagic, sizeof(badMagic));
        std::memcpy(bad.data() + sizeof(badMagic), &version, sizeof(version));
        files["bad/index.vfNavIndex"] = bad;

        uint32_t magic = navigation::NAVMESH_FILE_MAGIC;
        uint32_t oldVersion = 4; // below NAVMESH_TILE_MIN_SUPPORTED_VERSION
        std::vector<uint8_t> old(sizeof(magic) + sizeof(oldVersion));
        std::memcpy(old.data(), &magic, sizeof(magic));
        std::memcpy(old.data() + sizeof(magic), &oldVersion, sizeof(oldVersion));
        files["old/index.vfNavIndex"] = old;

        navigation::NavmeshTileIndex loaded;

        navigation::NavmeshTileCache badCache("bad");
        useInMemoryFiles(badCache, files);
        CHECK_FALSE(badCache.loadIndex(loaded));

        navigation::NavmeshTileCache oldCache("old");
        useInMemoryFiles(oldCache, files);
        CHECK_FALSE(oldCache.loadIndex(loaded));
    }

    TEST_CASE("LOD tiles round-trip through the injected reader")
    {
        TempDir dir("vf_test_navcache_lod");

        navigation::NavmeshTileLodKey lodKey{2, 3, 1};
        navigation::NavmeshTileData tile = makeTile(2, 3, 0x5A);
        tile.lod = 1;

        {
            navigation::NavmeshTileCache writer(dir.str());
            CHECK(writer.saveTile(lodKey, tile));
        }

        auto files = slurpDirectory(dir.path);
        fs::remove_all(dir.path);

        navigation::NavmeshTileCache cache(dir.str());
        useInMemoryFiles(cache, files);

        navigation::NavmeshTileData loaded;
        REQUIRE(cache.loadTile(lodKey, loaded));
        CHECK(loaded.lod == 1);
        CHECK(loaded.data[0] == 0x5A);
        CHECK(cache.hasTile(lodKey));
    }
}
