#include <doctest.h>
#include <world/HLODSerialization.hpp>
#include <serialization/SerializationFileAccess.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// .vfHLOD persistence: VFHL round-trip, header-only reads, and
// the bounds checks that replaced the ifstream failbit (VK-1587)
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path hlodTestRoot()
    {
        return fs::temp_directory_path() / "vf_hlod_serialization_tests";
    }

    void resetHlodTestRoot()
    {
        std::error_code ec;
        fs::remove_all(hlodTestRoot(), ec);
        fs::create_directories(hlodTestRoot(), ec);
    }

    resource::Vertex makeVertex(float seed)
    {
        resource::Vertex vertex;
        vertex.position = {seed, seed + 1.0f, seed + 2.0f};
        vertex.normal = {0.0f, 1.0f, 0.0f};
        vertex.texCoords = {seed * 0.5f, seed * 0.25f};
        return vertex;
    }

    world::HLODFileData makeHlodData()
    {
        world::HLODFileData data;
        data.header.tier = 1;
        data.header.cellSize = 2;
        data.header.cellX = -3;
        data.header.cellZ = 7;
        data.header.aabbMinX = -10.0f;
        data.header.aabbMaxX = 10.0f;
        data.header.submeshCount = 2;
        data.header.totalVertexCount = 3;
        data.header.totalIndexCount = 6;

        for (uint32_t i = 0; i < data.header.submeshCount; ++i)
        {
            world::HLODSubmeshInfo submesh;
            submesh.materialPath = "Assets/Materials/hlod_" + std::to_string(i) + ".vfMat";
            for (uint32_t lod = 0; lod < world::HLOD_LOD_LEVELS; ++lod)
            {
                submesh.lods[lod].vertexCount = lod + 1;
                submesh.lods[lod].indexCount = (lod + 1) * 3;
            }
            data.submeshes.push_back(std::move(submesh));
        }

        for (uint32_t i = 0; i < data.header.totalVertexCount; ++i)
            data.vertices.push_back(makeVertex(static_cast<float>(i)));
        data.indices = {0, 1, 2, 2, 1, 0};

        data.meshletData.meshletBlob = {0xDE, 0xAD, 0xBE, 0xEF};
        data.meshletData.meshletVertices = {0, 1, 2};
        data.meshletData.meshletPrimitives = {0, 1, 2, 3, 4, 5};
        return data;
    }

    std::vector<uint8_t> slurpHlod(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        REQUIRE(file.is_open());
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<uint8_t> bytes(size);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        return bytes;
    }

    // A compressed .vfpak entry: bytes available, no seekable physical location.
    class ScopedHlodArchive
    {
    public:
        explicit ScopedHlodArchive(std::unordered_map<std::string, std::vector<uint8_t>> entries)
            : contents(std::make_shared<Entries>(std::move(entries)))
        {
            auto shared = contents;
            serialization::setSerializationFileAccess({
                [](const std::string&) -> std::optional<serialization::SerializationFileLocation>
                {
                    return std::nullopt;
                },
                [shared](const std::string& path)
                {
                    auto it = shared->find(path);
                    return it == shared->end() ? std::vector<uint8_t>{} : it->second;
                },
                [shared](const std::string& path) { return shared->contains(path); },
                [] { return true; }});
        }

        ScopedHlodArchive(const ScopedHlodArchive&) = delete;
        ScopedHlodArchive& operator=(const ScopedHlodArchive&) = delete;

        ~ScopedHlodArchive() { serialization::resetSerializationFileAccess(); }

    private:
        using Entries = std::unordered_map<std::string, std::vector<uint8_t>>;
        std::shared_ptr<Entries> contents;
    };
}

TEST_SUITE("HLODSerialization")
{
    TEST_CASE("save then load round-trips every section")
    {
        resetHlodTestRoot();
        const auto written = makeHlodData();
        const std::string path = (hlodTestRoot() / "cell_-3_7_hlod1.vfHLOD").string();
        REQUIRE(world::HLODSerialization::save(path, written));

        world::HLODFileData read;
        REQUIRE(world::HLODSerialization::load(path, read));

        CHECK(read.header.tier == written.header.tier);
        CHECK(read.header.cellX == written.header.cellX);
        CHECK(read.header.cellZ == written.header.cellZ);
        CHECK(read.header.submeshCount == written.header.submeshCount);
        CHECK(read.header.aabbMaxX == doctest::Approx(written.header.aabbMaxX));

        REQUIRE(read.submeshes.size() == written.submeshes.size());
        for (size_t i = 0; i < read.submeshes.size(); ++i)
        {
            CHECK(read.submeshes[i].materialPath == written.submeshes[i].materialPath);
            for (uint32_t lod = 0; lod < world::HLOD_LOD_LEVELS; ++lod)
            {
                CHECK(read.submeshes[i].lods[lod].vertexCount ==
                      written.submeshes[i].lods[lod].vertexCount);
                CHECK(read.submeshes[i].lods[lod].indexCount ==
                      written.submeshes[i].lods[lod].indexCount);
            }
        }

        REQUIRE(read.vertices.size() == written.vertices.size());
        CHECK(read.vertices[2].position.x == doctest::Approx(written.vertices[2].position.x));
        CHECK(read.indices == written.indices);
        CHECK(read.meshletData.meshletBlob == written.meshletData.meshletBlob);
        CHECK(read.meshletData.meshletVertices == written.meshletData.meshletVertices);
        CHECK(read.meshletData.meshletPrimitives == written.meshletData.meshletPrimitives);
    }

    TEST_CASE("readHeader agrees with the header a full load produced")
    {
        resetHlodTestRoot();
        const std::string path = (hlodTestRoot() / "headeronly.vfHLOD").string();
        REQUIRE(world::HLODSerialization::save(path, makeHlodData()));

        world::HLODFileData full;
        REQUIRE(world::HLODSerialization::load(path, full));

        world::HLODFileHeader header{};
        REQUIRE(world::HLODSerialization::readHeader(path, header));
        CHECK(header.tier == full.header.tier);
        CHECK(header.cellX == full.header.cellX);
        CHECK(header.cellZ == full.header.cellZ);
        CHECK(header.submeshCount == full.header.submeshCount);
        CHECK(header.totalVertexCount == full.header.totalVertexCount);
        CHECK(header.totalIndexCount == full.header.totalIndexCount);
    }

    TEST_CASE("loadFromMemory rejects malformed buffers instead of over-allocating")
    {
        resetHlodTestRoot();
        const std::string path = (hlodTestRoot() / "corrupt.vfHLOD").string();
        REQUIRE(world::HLODSerialization::save(path, makeHlodData()));
        const auto good = slurpHlod(path);

        world::HLODFileData out;

        SUBCASE("empty buffer")
        {
            CHECK_FALSE(world::HLODSerialization::loadFromMemory({}, out));
        }

        SUBCASE("shorter than the 64-byte header")
        {
            std::vector<uint8_t> bytes(good.begin(), good.begin() + 32);
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }

        SUBCASE("wrong magic")
        {
            auto bytes = good;
            bytes[0] = 'X';
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }

        SUBCASE("header only, no submesh table")
        {
            std::vector<uint8_t> bytes(good.begin(),
                                       good.begin() + static_cast<ptrdiff_t>(world::HLOD_HEADER_SIZE));
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }

        SUBCASE("submesh count larger than the file could hold")
        {
            auto bytes = good;
            world::HLODFileHeader header{};
            REQUIRE(world::HLODSerialization::parseHeader(bytes, header));
            header.submeshCount = 0xFFFFFFFFu;
            std::memcpy(bytes.data(), &header, world::HLOD_HEADER_SIZE);
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }

        SUBCASE("vertex count larger than the file could hold")
        {
            auto bytes = good;
            world::HLODFileHeader header{};
            REQUIRE(world::HLODSerialization::parseHeader(bytes, header));
            header.totalVertexCount = 0xFFFFFFFFu;
            std::memcpy(bytes.data(), &header, world::HLOD_HEADER_SIZE);
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }

        SUBCASE("truncated mid-geometry")
        {
            std::vector<uint8_t> bytes(good.begin(), good.end() - 8);
            CHECK_FALSE(world::HLODSerialization::loadFromMemory(bytes, out));
        }
    }

    TEST_CASE("archive mode loads a proxy that exists only inside the pak")
    {
        resetHlodTestRoot();
        const std::string diskPath = (hlodTestRoot() / "packed.vfHLOD").string();
        REQUIRE(world::HLODSerialization::save(diskPath, makeHlodData()));
        const auto bytes = slurpHlod(diskPath);

        const std::string archiveKey = "Assets/Worlds/sectors/sector_2_-3_hlod0.vfHLOD";
        REQUIRE_FALSE(fs::exists(archiveKey));

        ScopedHlodArchive archive{{{archiveKey, bytes}}};

        world::HLODFileData data;
        REQUIRE(world::HLODSerialization::load(archiveKey, data));
        CHECK(data.header.submeshCount == 2);
        CHECK(data.vertices.size() == 3);

        // No physical location for a compressed entry — readHeader inflates instead of seeking
        world::HLODFileHeader header{};
        REQUIRE(world::HLODSerialization::readHeader(archiveKey, header));
        CHECK(header.cellZ == 7);

        world::HLODFileData missing;
        CHECK_FALSE(world::HLODSerialization::load("Assets/Worlds/sectors/nope.vfHLOD", missing));
    }
}
