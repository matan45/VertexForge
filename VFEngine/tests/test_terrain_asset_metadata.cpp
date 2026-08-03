#include <doctest.h>

#include "test_terrain_serializer_fixture.hpp"

#include "impl/scene/TerrainAssetMetadata.hpp"

#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <resource/AssetTypes.hpp>

#include <string>

// VK-1643: the incremental save must refresh .vfmeta with the same metadata contract as the
// full save. Both route through services::refreshTerrainSidecar(), so covering it here covers
// both paths — TerrainService itself cannot be constructed in a CPU-only test (it needs the
// scene graph and the render providers).
TEST_SUITE("TerrainAssetMetadata")
{
    TEST_CASE("refreshing the sidecar writes the terrain metadata contract")
    {
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<terrain::TileCoord> coords = {{0, 0}};
        auto grid = makePopulatedTerrainTestGrid(config, coords, true);
        ScopedTerrainTestFile file("metadata");
        REQUIRE(terrain::TerrainSerializer::save(
            makeTerrainTestSaveParams(file.string(), *grid, config)));

        const auto metaPath = asset::AssetMetadataSerializer::getMetaPath(file.string());
        // Sidecars append the extension rather than replacing it.
        CHECK(metaPath.string() == file.string() + ".vfmeta");
        REQUIRE_FALSE(terrain_test_fs::exists(metaPath));

        const auto guid = services::refreshTerrainSidecar(file.string());
        CHECK(guid.isValid());
        REQUIRE(terrain_test_fs::exists(metaPath));

        const auto meta = asset::AssetMetadataSerializer::load(metaPath);
        REQUIRE(meta.has_value());
        CHECK(meta->guid == guid);
        CHECK(meta->type == resource::AssetType::Terrain);
        CHECK(meta->importSourcePath == file.string());
        CHECK(meta->formatVersion == asset::AssetMetadata::kCurrentFormatVersion);
        CHECK_FALSE(meta->importTimestamp.empty());
    }

    TEST_CASE("refreshing an existing sidecar preserves its GUID")
    {
        // The GUID is what every AssetRef to this terrain resolves through, so a repeat save
        // must never mint a new one.
        const auto config = makeTerrainTestConfig(terrain::TileResolution::Low);
        const std::vector<terrain::TileCoord> coords = {{0, 0}};
        auto grid = makePopulatedTerrainTestGrid(config, coords, true);
        ScopedTerrainTestFile file("metadata-repeat");
        REQUIRE(terrain::TerrainSerializer::save(
            makeTerrainTestSaveParams(file.string(), *grid, config)));

        const auto first = services::refreshTerrainSidecar(file.string());
        REQUIRE(first.isValid());

        const auto second = services::refreshTerrainSidecar(file.string());
        CHECK(second.isValid());
        CHECK(second == first);

        const auto meta = asset::AssetMetadataSerializer::load(
            asset::AssetMetadataSerializer::getMetaPath(file.string()));
        REQUIRE(meta.has_value());
        CHECK(meta->guid == first);
    }
}
