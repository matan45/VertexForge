// VK-1653 — terrain authoring queries, the synchronous multi-terrain save and .vfTerrainMat
// authoring, driven through the REAL TerrainService handlers. CPU-only: createTerrain builds CPU
// tiles, the save and the load are plain file IO, and no brush compute provider is involved.
//
// The fixture follows test_terrain_layer_stack_ops.cpp. Every terrain lives in a ScopedTerrain
// declared AFTER the service, so it is deleted while the grid it owns still exists -- the
// EntityRegistry is process-global and ~TerrainService does not remove entities. The terrains are
// 2x2, never one tile: createGrid's LOD pass only reaches the JobSystem path that file's NOTE
// describes with more than one tile, and these cases keep exercising it.
//
// resolveAuthoringGrid is private. Its resolution is pinned through GetTerrainHeightsQuery (a
// failure leaves every sample invalid) and its messages through SaveTerrainsCommand, which returns
// them verbatim for an explicitly named entity.

#include <doctest.h>

#include "data/EntityConversion.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainMaterialAssetEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"
#include "impl/scene/TerrainService.hpp"
#include <asset/AssetDatabase.hpp>
#include <asset/AssetRef.hpp>
#include <components/Components.hpp>
#include <resource/EndianUtils.hpp>
#include <resource/ResourceManager.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <terrain/TerrainMaterialAsset.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include <terrain/TerrainTile.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace authoring_test_fs = std::filesystem;

    // Bilinear sampling over a plane is exact up to float rounding.
    constexpr float EXACT_TOLERANCE = 1e-4f;
    // Plus VFTR's per-tile uint16 height quantisation: a tile here spans at most 2.24 m, so one
    // step is ~3.4e-5 m.
    constexpr float RELOAD_TOLERANCE = 1e-3f;

    // A live TerrainService with its handlers, plus the one foreign query a load makes:
    // finishLoadTerrain ends in activateTilesForLoadedSectors, which asks IsWorldModeQuery, and the
    // dispatcher throws on a query nobody answers. The asset database is cleared on the way in, as
    // the other suites that register assets do; every path here is absolute, so no project root is
    // needed.
    struct AuthoringFixture
    {
        AuthoringFixture()
        {
            asset::AssetDatabase::instance().clear();

            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.clear();
            service.registerEventHandlers();
            dispatcher.registerQueryHandler<events::world::IsWorldModeQuery>(
                [](const events::world::IsWorldModeQuery&) { return false; });
        }

        ~AuthoringFixture()
        {
            events::EventDispatcher::instance().unregisterQueryHandler<events::world::IsWorldModeQuery>();
        }

        AuthoringFixture(const AuthoringFixture&) = delete;
        AuthoringFixture& operator=(const AuthoringFixture&) = delete;

        std::shared_ptr<scene::SceneGraphSystem> sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::TerrainService service{sceneGraph};
    };

    // Deletes a terrain BEFORE the service that owns its grid; see test_terrain_layer_stack_ops.cpp
    // for why that order is load-bearing. A terrain a case already deleted is a no-op here:
    // deleteTerrain refuses an entity that is no longer valid.
    class ScopedTerrain
    {
    public:
        ScopedTerrain(services::TerrainService& owner, services::EntityHandle terrain)
            : service(owner), entity(terrain)
        {
        }

        ~ScopedTerrain()
        {
            if (entity.isValid())
                service.deleteTerrain(entity);
        }

        ScopedTerrain(const ScopedTerrain&) = delete;
        ScopedTerrain& operator=(const ScopedTerrain&) = delete;

    private:
        services::TerrainService& service;
        services::EntityHandle entity;
    };

    // A bare entity outside the scene graph -- the shells, tiles and plain entities the resolution
    // cases need -- destroyed on the way out.
    class ScopedEntity
    {
    public:
        explicit ScopedEntity(const std::string& name) : entity(name) {}

        ~ScopedEntity()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (registry.valid(entity.getHandle()))
                registry.destroy(entity.getHandle());
        }

        ScopedEntity(const ScopedEntity&) = delete;
        ScopedEntity& operator=(const ScopedEntity&) = delete;

        [[nodiscard]] services::EntityHandle handle() const
        {
            return services::internal::toHandle(entity.getHandle());
        }

        scene::Entity entity;
    };

    class ScopedTempDirectory
    {
    public:
        explicit ScopedTempDirectory(const std::string& stem)
        {
            static std::atomic<uint64_t> sequence{0};
            const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            root = authoring_test_fs::temp_directory_path() /
                   ("vertexforge-vk1653-" + stem + "-" + std::to_string(stamp) + "-" +
                    std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            authoring_test_fs::create_directories(root);
        }

        ~ScopedTempDirectory()
        {
            std::error_code ec;
            authoring_test_fs::remove_all(root, ec);
        }

        ScopedTempDirectory(const ScopedTempDirectory&) = delete;
        ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;

        // Absolute, forward slashes -- the shape the MCP layer hands the engine.
        [[nodiscard]] std::string file(const std::string& relative) const
        {
            return (root / relative).generic_string();
        }

    private:
        authoring_test_fs::path root;
    };

    // Clears the read-only attribute again before ScopedTempDirectory tries to remove the file.
    class ScopedReadOnly
    {
    public:
        explicit ScopedReadOnly(std::string target) : path(std::move(target)) { set(true); }
        ~ScopedReadOnly() { set(false); }

        ScopedReadOnly(const ScopedReadOnly&) = delete;
        ScopedReadOnly& operator=(const ScopedReadOnly&) = delete;

    private:
        void set(bool readOnly) const
        {
            // MSVC maps the write bits onto FILE_ATTRIBUTE_READONLY only when ALL of them are gone.
            const auto writeBits = authoring_test_fs::perms::owner_write |
                                   authoring_test_fs::perms::group_write |
                                   authoring_test_fs::perms::others_write;
            std::error_code ec;
            authoring_test_fs::permissions(path, writeBits,
                                           readOnly ? authoring_test_fs::perm_options::remove
                                                    : authoring_test_fs::perm_options::add,
                                           ec);
        }

        std::string path;
    };

    services::TerrainCreationData twoByTwo()
    {
        // Tiles (-1,-1) (0,-1) (-1,0) (0,0): the terrain covers [-32, 32) on both axes, Low
        // resolution so a vertex sits on every whole metre.
        services::TerrainCreationData creation;
        creation.tilesX = 2;
        creation.tilesZ = 2;
        creation.resolution = 0;
        creation.worldTileSize = 32.0f;
        creation.maxHeight = 100.0f;
        creation.minHeight = -10.0f;
        return creation;
    }

    float planeHeight(float x, float z)
    {
        return 2.0f + 0.05f * x + 0.02f * z;
    }

    // Writes planeHeight into every resident tile the service owns. A function of world position,
    // so the vertices two tiles share agree by construction, and a plane, so any sample on the
    // terrain has a known answer.
    void writePlane(services::TerrainService& service)
    {
        for (terrain::TerrainTile* tile : service.getAllLoadedTiles())
        {
            REQUIRE(tile != nullptr);
            const uint32_t vpt = tile->config.getVertexCount();
            const float spacing = tile->config.getVertexSpacing();
            const float originX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize;
            const float originZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize;
            REQUIRE(tile->heightData.size() == static_cast<size_t>(vpt) * vpt);

            for (uint32_t vz = 0; vz < vpt; ++vz)
            {
                for (uint32_t vx = 0; vx < vpt; ++vx)
                {
                    tile->heightData[static_cast<size_t>(vz) * vpt + vx] =
                        planeHeight(originX + static_cast<float>(vx) * spacing,
                                    originZ + static_cast<float>(vz) * spacing);
                }
            }
        }
    }

    // One probe in each of the four tiles, plus the corner all four share and the far corner.
    const std::vector<glm::vec2>& probesOnEveryTile()
    {
        static const std::vector<glm::vec2> probes = {
            {5.5f, 7.25f},     // (0, 0)
            {-20.5f, 3.0f},    // (-1, 0)
            {12.0f, -31.0f},   // (0, -1)
            {-31.75f, -0.25f}, // (-1, -1)
            {0.0f, 0.0f},
            {31.5f, 31.5f},
        };
        return probes;
    }

    std::vector<services::TerrainHeightSample> heightsAt(services::EntityHandle terrain,
                                                         const std::vector<glm::vec2>& positions,
                                                         bool pageIn = true)
    {
        events::terrainAuthoring::GetTerrainHeightsQuery query;
        query.terrainEntity = terrain;
        query.positions = positions;
        query.pageIn = pageIn;
        return events::EventDispatcher::instance().query(query);
    }

    void checkPlane(const std::vector<services::TerrainHeightSample>& samples,
                    const std::vector<glm::vec2>& positions, float tolerance)
    {
        REQUIRE(samples.size() == positions.size());
        for (size_t i = 0; i < samples.size(); ++i)
        {
            CAPTURE(i);
            CHECK(samples[i].onTerrain);
            REQUIRE(samples[i].valid);
            CHECK(std::abs(samples[i].height - planeHeight(positions[i].x, positions[i].y)) <= tolerance);
        }
    }

    std::vector<services::TerrainSaveOutcome> saveTerrains(std::vector<services::TerrainSaveRequest> requests)
    {
        events::terrainAuthoring::SaveTerrainsCommand cmd;
        cmd.requests = std::move(requests);
        return events::EventDispatcher::instance().execute(cmd);
    }

    std::vector<services::TerrainSummary> listTerrains()
    {
        return events::EventDispatcher::instance().query(events::terrainAuthoring::ListTerrainsQuery{});
    }

    std::optional<services::TerrainSummary> summaryOf(const std::vector<services::TerrainSummary>& list,
                                                      services::EntityHandle terrain)
    {
        for (const auto& summary : list)
        {
            if (summary.entity == terrain)
                return summary;
        }
        return std::nullopt;
    }

    bool isSaveLocked()
    {
        return events::EventDispatcher::instance().query(events::terrainAuthoring::IsTerrainSaveLockedQuery{});
    }

    void setSaveLock(bool locked)
    {
        events::terrain::SetTerrainSaveLockCommand cmd;
        cmd.locked = locked;
        events::EventDispatcher::instance().execute(cmd);
    }

    services::EntityHandle loadTerrain(const std::string& path)
    {
        events::terrain::LoadTerrainCommand cmd;
        cmd.path = path;
        return events::EventDispatcher::instance().execute(cmd);
    }

    void setTerrainMaterial(services::EntityHandle terrain, const std::string& materialPath)
    {
        events::terrain::SetTerrainMaterialPathCommand cmd;
        cmd.terrainEntity = terrain;
        cmd.materialPath = materialPath;
        events::EventDispatcher::instance().execute(cmd);
    }

    services::EntityHandle firstTileOf(services::EntityHandle terrain)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity ent = services::internal::fromHandle(terrain);
        REQUIRE(registry.all_of<components::ChildrenComponent>(ent));
        for (const entt::entity child : registry.get<components::ChildrenComponent>(ent).children)
        {
            if (registry.valid(child) && registry.all_of<components::TerrainTileComponent>(child))
                return services::internal::toHandle(child);
        }
        FAIL("the terrain has no tile entity");
        return services::EntityHandle::invalid();
    }

    components::TerrainComponent& terrainComponentOf(services::EntityHandle terrain)
    {
        return scene::EntityRegistry::getRegistry().get<components::TerrainComponent>(
            services::internal::fromHandle(terrain));
    }

    bool contains(const std::string& haystack, std::string_view needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    bool sameFile(const std::string& a, const std::string& b)
    {
        std::error_code ec;
        const bool same = authoring_test_fs::equivalent(a, b, ec);
        return same && !ec;
    }

    void writeFile(const std::string& path, const std::string& content)
    {
        std::error_code ec;
        authoring_test_fs::create_directories(authoring_test_fs::path(path).parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << content;
    }

    // The exact layout HeightmapLoader::loadVFImage reads: an uncompressed BGRA mip 0.
    void writeVFImage(const std::string& path, uint32_t width, uint32_t height, uint8_t grey)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        resource::endian::writeLE<uint8_t>(out, 0);        // file type
        resource::endian::writeLE<uint32_t>(out, 1);       // version major
        resource::endian::writeLE<uint32_t>(out, 0);       // version minor
        resource::endian::writeLE<uint32_t>(out, 0);       // version patch
        resource::endian::writeLE<uint32_t>(out, width);
        resource::endian::writeLE<uint32_t>(out, height);
        resource::endian::writeLE<uint32_t>(out, 4);       // channels
        resource::endian::writeLE<uint32_t>(out, 1);       // mip levels
        resource::endian::writeLE<uint8_t>(out, 0);        // compression: none
        resource::endian::writeLE<uint32_t>(out, width);   // mip 0 width
        resource::endian::writeLE<uint32_t>(out, height);  // mip 0 height
        resource::endian::writeLE<uint32_t>(out, width * height * 4);
        for (uint32_t i = 0; i < width * height; ++i)
        {
            const uint8_t bgra[4] = {grey, grey, grey, 255};
            out.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
        }
        REQUIRE(out.good());
    }

    services::HeightmapProbeResult probeHeightmap(const std::string& path)
    {
        events::terrainAuthoring::ProbeHeightmapQuery query;
        query.path = path;
        return events::EventDispatcher::instance().query(query);
    }

    services::CreateTerrainMaterialAssetResult createMaterial(const std::string& directory,
                                                              const std::string& name,
                                                              services::TerrainMaterialLayerPatch baseLayer = {})
    {
        events::terrainMaterial::CreateTerrainMaterialAssetCommand cmd;
        cmd.directory = directory;
        cmd.name = name;
        cmd.baseLayer = std::move(baseLayer);
        return events::EventDispatcher::instance().execute(cmd);
    }

    services::TerrainMaterialEditResult editLayer(const std::string& materialPath,
                                                  std::optional<uint32_t> index,
                                                  services::TerrainMaterialLayerPatch patch)
    {
        events::terrainMaterial::EditTerrainMaterialLayerCommand cmd;
        cmd.materialPath = materialPath;
        cmd.index = index;
        cmd.patch = std::move(patch);
        return events::EventDispatcher::instance().execute(cmd);
    }

    std::optional<services::TerrainMaterialInfo> materialInfo(const std::string& materialPath)
    {
        events::terrainMaterial::GetTerrainMaterialInfoQuery query;
        query.materialPath = materialPath;
        return events::EventDispatcher::instance().query(query);
    }

    // Counts what the material ops announce. Subscribed for the life of a case; the lambdas capture
    // `this`, so the counter never moves.
    struct MaterialNotifications
    {
        MaterialNotifications()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            savedSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::resource::AssetSavedNotification>(
                    [this](const events::resource::AssetSavedNotification& notification)
                    {
                        ++assetSaved;
                        lastSavedPath = notification.filePath;
                    }));
            compiledSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::terrain::TerrainMaterialCompiledNotification>(
                    [this](const events::terrain::TerrainMaterialCompiledNotification&)
                    {
                        ++materialCompiled;
                    }));
        }

        MaterialNotifications(const MaterialNotifications&) = delete;
        MaterialNotifications& operator=(const MaterialNotifications&) = delete;

        int assetSaved = 0;
        int materialCompiled = 0;
        std::string lastSavedPath;
        events::ScopedSubscription savedSubscription;
        events::ScopedSubscription compiledSubscription;
    };
}

TEST_SUITE("TerrainAuthoringQueries")
{
    TEST_CASE("terrain_authoring: an authoring command resolves none, one, two, a tile, a shell and a non-terrain")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("resolve");
        const std::vector<glm::vec2> probe = {{5.5f, 7.25f}};
        const float expected = planeHeight(5.5f, 7.25f);

        // None: nothing to resolve to.
        {
            const auto samples = heightsAt(services::EntityHandle::invalid(), probe);
            REQUIRE(samples.size() == 1);
            CHECK_FALSE(samples[0].valid);
            CHECK_FALSE(samples[0].onTerrain);
        }

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        writePlane(fixture.service);

        // One: an invalid id means the only live terrain.
        {
            const auto samples = heightsAt(services::EntityHandle::invalid(), probe);
            REQUIRE(samples.size() == 1);
            REQUIRE(samples[0].valid);
            CHECK(std::abs(samples[0].height - expected) <= EXACT_TOLERANCE);
        }

        // A tile resolves to its terrain through the scene-graph parent.
        {
            const services::EntityHandle tile = firstTileOf(t1);
            const auto samples = heightsAt(tile, probe);
            REQUIRE(samples.size() == 1);
            REQUIRE(samples[0].valid);
            CHECK(std::abs(samples[0].height - expected) <= EXACT_TOLERANCE);

            const auto outcomes = saveTerrains({{tile, dir.file("from-tile.vfTerrain")}});
            REQUIRE(outcomes.size() == 1);
            CHECK(outcomes[0].success);
            CHECK(outcomes[0].terrainEntity == t1);
        }

        ScopedEntity shell("Shell");
        shell.entity.addComponent<components::TerrainComponent>();

        ScopedEntity plain("Plain");

        ScopedEntity shellTile("Tile_0_0");
        shellTile.entity.addComponent<components::TerrainTileComponent>();
        shell.entity.addChildren(shellTile.entity);

        ScopedEntity orphanTile("Tile_9_9");
        orphanTile.entity.addComponent<components::TerrainTileComponent>();

        services::EntityHandle stale;
        {
            scene::Entity doomed("Doomed");
            stale = services::internal::toHandle(doomed.getHandle());
            scene::EntityRegistry::getRegistry().destroy(doomed.getHandle());
        }

        // Explicit ids that are not a loaded terrain resolve to nothing...
        for (const services::EntityHandle bad : {shell.handle(), plain.handle(), shellTile.handle(),
                                                 orphanTile.handle(), stale})
        {
            CAPTURE(bad.id);
            const auto samples = heightsAt(bad, probe);
            REQUIRE(samples.size() == 1);
            CHECK_FALSE(samples[0].valid);
            CHECK_FALSE(samples[0].onTerrain);
        }

        // ...and say why. A save request carries the resolution message through verbatim.
        {
            const auto outcomes = saveTerrains({
                {shell.handle(), dir.file("shell.vfTerrain")},
                {plain.handle(), dir.file("plain.vfTerrain")},
                {shellTile.handle(), dir.file("shell-tile.vfTerrain")},
                {orphanTile.handle(), dir.file("orphan-tile.vfTerrain")},
                {stale, dir.file("stale.vfTerrain")},
                {services::EntityHandle::invalid(), dir.file("nothing.vfTerrain")},
            });
            REQUIRE(outcomes.size() == 6);
            for (const auto& outcome : outcomes)
                CHECK_FALSE(outcome.success);

            CHECK(outcomes[0].error == "entity " + std::to_string(shell.handle().id) +
                                       " is a terrain with no data loaded (an empty shell)");
            CHECK(outcomes[1].error == "entity " + std::to_string(plain.handle().id) + " is not a terrain");
            CHECK(outcomes[2].error == "entity " + std::to_string(shellTile.handle().id) + " is a tile of terrain " +
                                       std::to_string(shell.handle().id) +
                                       ", which has no data loaded (an empty shell)");
            CHECK(outcomes[3].error == "entity " + std::to_string(orphanTile.handle().id) +
                                       " is a terrain tile with no parent terrain");
            CHECK(outcomes[4].error == "entity " + std::to_string(stale.id) + " is not a terrain (no such entity)");
            CHECK(outcomes[5].error == "the save request names no terrain");

            CHECK_FALSE(authoring_test_fs::exists(dir.file("shell.vfTerrain")));
            CHECK_FALSE(authoring_test_fs::exists(dir.file("nothing.vfTerrain")));
        }

        // Two: an invalid id is now ambiguous, an explicit one still resolves.
        const services::EntityHandle t2 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t2.isValid());
        ScopedTerrain guard2(fixture.service, t2);
        {
            const auto ambiguous = heightsAt(services::EntityHandle::invalid(), probe);
            REQUIRE(ambiguous.size() == 1);
            CHECK_FALSE(ambiguous[0].valid);
            CHECK_FALSE(ambiguous[0].onTerrain);

            const auto first = heightsAt(t1, probe);
            REQUIRE(first[0].valid);
            CHECK(std::abs(first[0].height - expected) <= EXACT_TOLERANCE);

            // Created after writePlane, so still flat at 0.
            const auto second = heightsAt(t2, probe);
            REQUIRE(second[0].valid);
            CHECK(second[0].height == 0.0f);
        }
    }

    TEST_CASE("terrain_authoring: ListTerrains reports live terrains and empty shells in id order")
    {
        AuthoringFixture fixture;

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);

        ScopedEntity shell("Shell");
        shell.entity.addComponent<components::TerrainComponent>();
        ScopedEntity plain("Plain");

        auto list = listTerrains();
        CHECK(std::is_sorted(list.begin(), list.end(),
                             [](const services::TerrainSummary& a, const services::TerrainSummary& b)
                             { return a.entity.id < b.entity.id; }));
        CHECK_FALSE(summaryOf(list, plain.handle()).has_value());

        const auto live = summaryOf(list, t1);
        REQUIRE(live.has_value());
        CHECK(live->live);
        CHECK(live->name == "Terrain");
        CHECK(live->data.tileCount == 4);
        CHECK(live->residentHeightTiles == 4);
        CHECK(live->pendingMeshTiles == 0); // createGrid meshed every tile
        CHECK_FALSE(live->streamingEnabled);
        CHECK_FALSE(live->hasCollider);     // no physics provider in a CPU test
        CHECK(live->data.savePath.empty());
        CHECK_FALSE(live->data.saveDirty);

        const auto empty = summaryOf(list, shell.handle());
        REQUIRE(empty.has_value());
        CHECK_FALSE(empty->live);
        CHECK(empty->name == "Shell");
        CHECK(empty->residentHeightTiles == 0);
        CHECK(empty->pendingMeshTiles == 0);

        // pendingMeshTiles counts exactly what regenerateDirtyTiles still owes: isDirty with a
        // dirty LOD bit. Fresh heights with nothing to remesh are not pending.
        const auto tiles = fixture.service.getAllLoadedTiles();
        REQUIRE(tiles.size() == 4);
        tiles[0]->setAllLODsDirty();
        tiles[1]->isDirty = true;
        tiles[1]->dirtyLODMask = 0;

        events::terrain::SetTerrainStreamingEnabledCommand streaming;
        streaming.terrainEntity = t1;
        streaming.enabled = true;
        events::EventDispatcher::instance().execute(streaming);

        list = listTerrains();
        const auto updated = summaryOf(list, t1);
        REQUIRE(updated.has_value());
        CHECK(updated->pendingMeshTiles == 1);
        CHECK(updated->streamingEnabled);

        streaming.enabled = false;
        events::EventDispatcher::instance().execute(streaming);
    }

    TEST_CASE("terrain_authoring: GetTerrainHeights answers in input order, on and off the terrain")
    {
        AuthoringFixture fixture;

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        writePlane(fixture.service);

        CHECK(heightsAt(t1, {}).empty());

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::vector<glm::vec2> positions = {
            {5.5f, 7.25f},     // on
            {-40.0f, 0.0f},    // off, -X
            {31.75f, -31.75f}, // on, near a corner
            {32.0f, 0.0f},     // off: the +X edge belongs to tile (1, 0), which does not exist
            {-32.0f, -32.0f},  // on: the min corner belongs to tile (-1, -1)
            {nan, 0.0f},       // rejected, never wrapped
            {0.0f, 40.0f},     // off, +Z
            {-0.25f, 0.25f},   // on, one step across the seam
        };
        const std::vector<bool> on = {true, false, true, false, true, false, false, true};

        const auto samples = heightsAt(t1, positions);
        REQUIRE(samples.size() == positions.size());
        for (size_t i = 0; i < positions.size(); ++i)
        {
            CAPTURE(i);
            CHECK(samples[i].onTerrain == on[i]);
            CHECK(samples[i].valid == on[i]);
            if (on[i])
                CHECK(std::abs(samples[i].height - planeHeight(positions[i].x, positions[i].y)) <= EXACT_TOLERANCE);
            else
                CHECK(samples[i].height == 0.0f);
        }
    }

    TEST_CASE("terrain_authoring: SaveTerrains writes a terrain that reloads with the same heights")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("roundtrip");

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        writePlane(fixture.service);
        terrainComponentOf(t1).saveDirty = true;

        const std::string path = dir.file("terrains/hills.vfTerrain"); // the folder does not exist yet

        // Flush before unlock: the component update and TerrainSavedNotification must land while
        // the lock is still held.
        int savedNotifications = 0;
        bool lockedDuringFlush = false;
        std::string savePathDuringFlush;
        events::ScopedSubscription savedSubscription(
            events::EventDispatcher::instance().subscribe<events::terrain::TerrainSavedNotification>(
                [&](const events::terrain::TerrainSavedNotification& notification)
                {
                    ++savedNotifications;
                    lockedDuringFlush = isSaveLocked();
                    savePathDuringFlush = terrainComponentOf(notification.terrainEntity).savePath;
                }));

        const auto outcomes = saveTerrains({{t1, path}});
        REQUIRE(outcomes.size() == 1);
        CHECK(outcomes[0].success);
        CHECK(outcomes[0].error.empty());
        CHECK_FALSE(outcomes[0].incremental); // nothing on disk to patch yet
        CHECK(outcomes[0].terrainEntity == t1);
        CHECK(outcomes[0].path == path);
        REQUIRE(authoring_test_fs::exists(path));

        CHECK(savedNotifications == 1);
        CHECK(lockedDuringFlush);
        CHECK(savePathDuringFlush == path);
        CHECK_FALSE(isSaveLocked());

        const auto data = fixture.service.getTerrainData(t1);
        REQUIRE(data.has_value());
        CHECK(data->savePath == path);
        CHECK_FALSE(data->saveDirty);

        const services::EntityHandle loaded = loadTerrain(path);
        REQUIRE(loaded.isValid());
        ScopedTerrain guardLoaded(fixture.service, loaded);

        const auto& probes = probesOnEveryTile();

        // A reloaded, non-streaming terrain is all metadata: every tile resident, no heights. This is
        // the case GetTerrainHeightAtQuery answers with valid=false.
        {
            const auto cold = heightsAt(loaded, probes, /*pageIn=*/false);
            REQUIRE(cold.size() == probes.size());
            for (const auto& sample : cold)
            {
                CHECK(sample.onTerrain);
                CHECK_FALSE(sample.valid);
            }

            const auto before = summaryOf(listTerrains(), loaded);
            REQUIRE(before.has_value());
            CHECK(before->residentHeightTiles == 0);
        }

        checkPlane(heightsAt(loaded, probes), probes, RELOAD_TOLERANCE);
        checkPlane(heightsAt(t1, probes), probes, EXACT_TOLERANCE);

        const auto after = summaryOf(listTerrains(), loaded);
        REQUIRE(after.has_value());
        CHECK(after->residentHeightTiles == 4);

        // Streamed out entirely: on the terrain because the file has the tile, valid only once it is
        // paged back in.
        REQUIRE(fixture.service.streamOutTile(loaded, 0, 0));
        const std::vector<glm::vec2> inStreamedTile = {{5.5f, 7.25f}};
        {
            const auto cold = heightsAt(loaded, inStreamedTile, /*pageIn=*/false);
            REQUIRE(cold.size() == 1);
            CHECK(cold[0].onTerrain);
            CHECK_FALSE(cold[0].valid);
        }
        checkPlane(heightsAt(loaded, inStreamedTile), inStreamedTile, RELOAD_TOLERANCE);
    }

    TEST_CASE("terrain_authoring: SaveTerrains goes incremental only into the terrain's own file")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("incremental");
        const std::string fileA = dir.file("a.vfTerrain");
        const std::string fileB = dir.file("b.vfTerrain");
        const std::string fileC = dir.file("c.vfTerrain");

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        writePlane(fixture.service);

        auto outcomes = saveTerrains({{t1, fileA}});
        REQUIRE(outcomes.size() == 1);
        CHECK(outcomes[0].success);
        CHECK_FALSE(outcomes[0].incremental);

        outcomes = saveTerrains({{t1, fileA}});
        CHECK(outcomes[0].success);
        CHECK(outcomes[0].incremental); // its own, existing file

        outcomes = saveTerrains({{t1, fileB}});
        CHECK(outcomes[0].success);
        CHECK_FALSE(outcomes[0].incremental); // Save As to a new file

        outcomes = saveTerrains({{t1, fileA}});
        CHECK(outcomes[0].success);
        CHECK_FALSE(outcomes[0].incremental); // A exists, but t1 now streams from B

        // Another terrain's file is refused while that terrain is live...
        const services::EntityHandle t2 = fixture.service.createTerrain(twoByTwo()); // flat
        REQUIRE(t2.isValid());
        ScopedTerrain guard2(fixture.service, t2);
        outcomes = saveTerrains({{t2, fileC}});
        REQUIRE(outcomes[0].success);

        outcomes = saveTerrains({{t1, fileC}});
        CHECK_FALSE(outcomes[0].success);
        CHECK(contains(outcomes[0].error, "belongs to terrain " + std::to_string(t2.id)));
        CHECK_FALSE(isSaveLocked());

        // ...and once it is gone the existing file is rewritten whole, never patched: the reload
        // carries t1's plane, not the flat ground t2 left there.
        REQUIRE(fixture.service.deleteTerrain(t2));
        outcomes = saveTerrains({{t1, fileC}});
        CHECK(outcomes[0].success);
        CHECK_FALSE(outcomes[0].incremental);

        const services::EntityHandle reloaded = loadTerrain(fileC);
        REQUIRE(reloaded.isValid());
        ScopedTerrain guardReloaded(fixture.service, reloaded);
        checkPlane(heightsAt(reloaded, probesOnEveryTile()), probesOnEveryTile(), RELOAD_TOLERANCE);
    }

    TEST_CASE("terrain_authoring: SaveTerrains refuses under the lock, rejects bad requests and always unlocks")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("refusals");

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        const services::EntityHandle t2 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t2.isValid());
        ScopedTerrain guard2(fixture.service, t2);

        // Held by someone else (the drawer's worker save): everything is refused, nothing is written,
        // and the refusal does not release a lock it never took.
        {
            setSaveLock(true);
            const auto outcomes = saveTerrains({{t1, dir.file("locked-1.vfTerrain")},
                                                {t2, dir.file("locked-2.vfTerrain")}});
            REQUIRE(outcomes.size() == 2);
            for (const auto& outcome : outcomes)
            {
                CHECK_FALSE(outcome.success);
                CHECK(outcome.error == "a terrain save or load is already in progress");
            }
            CHECK(isSaveLocked());
            CHECK_FALSE(authoring_test_fs::exists(dir.file("locked-1.vfTerrain")));
            setSaveLock(false);
        }

        // Bad requests fail one by one; the good one in the same call still saves. The blocker passes
        // validation and so claims t1 before its folder fails -- which is why the good request names
        // t2: a second request for t1 in this call would be refused as a duplicate.
        const std::string good = dir.file("good.vfTerrain");
        {
            writeFile(dir.file("blocker"), "a file where a folder is needed");

            const auto outcomes = saveTerrains({
                {t1, ""},
                {t1, "relative.vfTerrain"},
                {t1, dir.file("wrong.png")},
                {t1, dir.file("blocker/sub/t.vfTerrain")},
                {t2, good},
            });
            REQUIRE(outcomes.size() == 5);
            CHECK(contains(outcomes[0].error, "no path given"));
            CHECK(contains(outcomes[1].error, "is not an absolute path"));
            CHECK(contains(outcomes[2].error, "is not a .vfTerrain path"));
            CHECK(contains(outcomes[3].error, "could not create the folder"));
            for (size_t i = 0; i < 4; ++i)
                CHECK_FALSE(outcomes[i].success);
            CHECK(outcomes[4].success);
            CHECK(terrainComponentOf(t1).savePath.empty());
            CHECK(terrainComponentOf(t2).savePath == good);
            CHECK_FALSE(isSaveLocked());
        }

        // One target, two terrains: the first claim wins, the second is refused rather than written
        // over it.
        {
            const std::string shared = dir.file("shared.vfTerrain");
            const auto outcomes = saveTerrains({{t1, shared}, {t2, shared}});
            REQUIRE(outcomes.size() == 2);
            CHECK(outcomes[0].success);
            CHECK_FALSE(outcomes[1].success);
            CHECK(contains(outcomes[1].error, "is the target of another terrain in this save"));
            CHECK(terrainComponentOf(t1).savePath == shared);
            CHECK(terrainComponentOf(t2).savePath == good);
        }

        // One terrain, two targets.
        {
            const auto outcomes = saveTerrains({{t2, dir.file("e.vfTerrain")}, {t2, dir.file("f.vfTerrain")}});
            REQUIRE(outcomes.size() == 2);
            CHECK(outcomes[0].success);
            CHECK_FALSE(outcomes[1].success);
            CHECK(contains(outcomes[1].error, "appears more than once"));
            CHECK_FALSE(authoring_test_fs::exists(dir.file("f.vfTerrain")));
        }

        CHECK_FALSE(isSaveLocked());
    }

    TEST_CASE("terrain_authoring: ProbeHeightmap explains why a heightmap will not load")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("probe");

        const std::string good = dir.file("hills.vfImage");
        writeVFImage(good, 4, 2, 128);
        {
            const auto probe = probeHeightmap(good);
            CHECK(probe.valid);
            CHECK(probe.width == 4);
            CHECK(probe.height == 2);
            CHECK(probe.error.empty());
        }

        {
            const auto probe = probeHeightmap("");
            CHECK_FALSE(probe.valid);
            CHECK_FALSE(probe.error.empty());
        }

        {
            const std::string png = dir.file("hills.png");
            writeFile(png, "not really a png");
            const auto probe = probeHeightmap(png);
            CHECK_FALSE(probe.valid);
            CHECK(contains(probe.error, "is not a .vfImage"));
        }

        {
            const auto probe = probeHeightmap(dir.file("missing.vfImage"));
            CHECK_FALSE(probe.valid);
            CHECK(contains(probe.error, "not found"));
        }

        {
            const std::string garbage = dir.file("garbage.vfImage");
            writeFile(garbage, "xyz");
            const auto probe = probeHeightmap(garbage);
            CHECK_FALSE(probe.valid);
            CHECK(contains(probe.error, "could not decode"));
        }
    }

    TEST_CASE("terrain_authoring: terrain material layers are created, appended, patched and capped in the shared instance")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("material");

        const std::string grass = dir.file("materials/grass.vfMat");
        const std::string rock = dir.file("materials/rock.vfMatInstance");
        const std::string notes = dir.file("materials/notes.png");
        writeFile(grass, "{}");
        writeFile(rock, "{}");
        writeFile(notes, "png");
        const std::string terrainsDir = dir.file("terrains"); // does not exist yet

        MaterialNotifications notifications;

        services::TerrainMaterialLayerPatch base;
        base.name = "Grass";
        base.materialPath = grass;
        base.tilingScale = 4.0f;

        const auto created = createMaterial(terrainsDir, "Ground", base);
        REQUIRE(created.success);
        CHECK(created.error.empty());
        CHECK(contains(created.path, "Ground.vfTerrainMat"));
        REQUIRE(authoring_test_fs::exists(created.path));
        CHECK(asset::AssetRef::fromPath(created.path).isValid());
        CHECK(notifications.assetSaved == 1);
        CHECK(notifications.lastSavedPath == created.path);
        CHECK(notifications.materialCompiled == 0);

        // Never overwrites: the same name again gets the next free one.
        {
            const auto second = createMaterial(terrainsDir, "Ground");
            REQUIRE(second.success);
            CHECK(contains(second.path, "Ground_1.vfTerrainMat"));
            CHECK(notifications.assetSaved == 2);
        }

        {
            const auto info = materialInfo(created.path);
            REQUIRE(info.has_value());
            CHECK(info->name == "Ground");
            CHECK(info->activeLayerCount == 1);
            CHECK(info->maxLayers == static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS));
            REQUIRE(info->layers.size() == 1);
            CHECK(info->layers[0].name == "Grass");
            CHECK(info->layers[0].tilingScale == 4.0f);
            CHECK(info->layers[0].enabled);
            CHECK_FALSE(info->layers[0].heightBlend);
            CHECK(sameFile(info->layers[0].materialPath, grass));
        }

        // Held across every edit below: the editor window and the renderer hold exactly this
        // instance, so it is what has to change.
        const auto cached = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(created.path));
        REQUIRE(cached);

        // Append.
        {
            services::TerrainMaterialLayerPatch patch;
            patch.name = "Rock";
            patch.materialPath = rock;
            patch.heightBlend = true;
            patch.heightContrast = 8.0f;

            const auto edit = editLayer(created.path, std::nullopt, patch);
            REQUIRE(edit.success);
            CHECK(edit.index == 1);
            CHECK(edit.material.activeLayerCount == 2);
            REQUIRE(edit.material.layers.size() == 2);
            CHECK(edit.material.layers[1].name == "Rock");
            CHECK(edit.material.layers[1].heightBlend);
            CHECK(edit.material.layers[1].heightContrast == 8.0f);
            CHECK(sameFile(edit.material.layers[1].materialPath, rock));

            CHECK(cached->activeLayerCount == 2);
            CHECK(cached->layers[1].name == "Rock");
            CHECK(notifications.assetSaved == 3);
            CHECK(notifications.materialCompiled == 1);

            const auto onDisk = terrain::TerrainMaterialAsset::load(created.path);
            REQUIRE(onDisk.has_value());
            CHECK(onDisk->activeLayerCount == 2);
            CHECK(onDisk->layers[1].blendMode == terrain::TerrainLayerBlendMode::HeightBlend);
        }

        // Patch one field; the rest of the layer stays as it was.
        {
            services::TerrainMaterialLayerPatch patch;
            patch.tilingScale = 2.5f;
            const auto edit = editLayer(created.path, 0u, patch);
            REQUIRE(edit.success);
            CHECK(edit.index == 0);
            CHECK(cached->layers[0].tilingScale == 2.5f);
            CHECK(cached->layers[0].name == "Grass");
            CHECK(cached->layers[0].materialRef == asset::AssetRef::fromPath(grass));
            CHECK(notifications.materialCompiled == 2);
        }

        // Refusals change nothing and announce nothing.
        {
            const int savedBefore = notifications.assetSaved;
            const int compiledBefore = notifications.materialCompiled;

            const auto refused = [&](std::optional<uint32_t> index, services::TerrainMaterialLayerPatch patch)
            {
                const auto edit = editLayer(created.path, index, std::move(patch));
                CHECK_FALSE(edit.success);
                CHECK_FALSE(edit.error.empty());
                return edit.error;
            };

            services::TerrainMaterialLayerPatch missing;
            missing.materialPath = dir.file("materials/missing.vfMat");
            CHECK(contains(refused(std::nullopt, missing), "material not found"));

            services::TerrainMaterialLayerPatch wrongType;
            wrongType.materialPath = notes;
            CHECK(contains(refused(std::nullopt, wrongType), "is not a .vfMat or .vfMatInstance"));

            for (const float tiling : {0.001f, 100.5f, std::numeric_limits<float>::quiet_NaN()})
            {
                services::TerrainMaterialLayerPatch patch;
                patch.tilingScale = tiling;
                CHECK(contains(refused(0u, patch), "tilingScale"));
            }

            for (const float contrast : {-0.5f, terrain::MAX_HEIGHT_BLEND_CONTRAST + 0.5f})
            {
                services::TerrainMaterialLayerPatch patch;
                patch.heightContrast = contrast;
                CHECK(contains(refused(0u, patch), "heightContrast"));
            }

            services::TerrainMaterialLayerPatch rename;
            rename.name = "";
            CHECK(contains(refused(0u, rename), "name cannot be empty"));

            CHECK(contains(refused(5u, services::TerrainMaterialLayerPatch{}), "does not exist"));

            CHECK(cached->activeLayerCount == 2);
            CHECK(cached->layers[0].tilingScale == 2.5f);
            CHECK(notifications.assetSaved == savedBefore);
            CHECK(notifications.materialCompiled == compiledBefore);
        }

        // Hiding one layer is fine; hiding the last visible one is refused, as in the editor.
        {
            services::TerrainMaterialLayerPatch hide;
            hide.enabled = false;
            REQUIRE(editLayer(created.path, 0u, hide).success);
            CHECK_FALSE(cached->layers[0].enabled);

            const auto last = editLayer(created.path, 1u, hide);
            CHECK_FALSE(last.success);
            CHECK(contains(last.error, "at least one layer must stay enabled"));
            CHECK(cached->layers[1].enabled);

            services::TerrainMaterialLayerPatch show;
            show.enabled = true;
            REQUIRE(editLayer(created.path, 0u, show).success);
        }

        // Fill to the 32-layer palette and one more.
        while (cached->activeLayerCount < terrain::MAX_TERRAIN_LAYERS)
            REQUIRE(editLayer(created.path, std::nullopt, services::TerrainMaterialLayerPatch{}).success);

        CHECK(cached->layers[2].name == "Layer 2"); // the editor's default name for a new slot
        {
            const auto full = editLayer(created.path, std::nullopt, services::TerrainMaterialLayerPatch{});
            CHECK_FALSE(full.success);
            CHECK(contains(full.error, "maximum of 32 layers"));
            CHECK(cached->activeLayerCount == terrain::MAX_TERRAIN_LAYERS);

            const auto info = materialInfo(created.path);
            REQUIRE(info.has_value());
            CHECK(info->layers.size() == static_cast<size_t>(terrain::MAX_TERRAIN_LAYERS));
        }

        CHECK_FALSE(materialInfo(dir.file("terrains/missing.vfTerrainMat")).has_value());

        // Create refusals.
        CHECK_FALSE(createMaterial(terrainsDir, "a/b").success);
        CHECK_FALSE(createMaterial(terrainsDir, "").success);
        CHECK_FALSE(createMaterial("relative/terrains", "Ground").success);
        {
            services::TerrainMaterialLayerPatch hidden;
            hidden.enabled = false;
            CHECK_FALSE(createMaterial(terrainsDir, "Hidden", hidden).success);
            CHECK_FALSE(authoring_test_fs::exists(dir.file("terrains/Hidden.vfTerrainMat")));
        }
    }

    TEST_CASE("terrain_authoring: a failed material save rolls the shared instance back")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("rollback");

        const auto created = createMaterial(dir.file("terrains"), "Locked");
        REQUIRE(created.success);
        const auto cached = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(created.path));
        REQUIRE(cached);

        MaterialNotifications notifications;
        {
            ScopedReadOnly readOnly(created.path);
            if (std::ofstream(created.path, std::ios::binary | std::ios::app).is_open())
            {
                MESSAGE("the file system ignored the read-only attribute; the rollback cannot be staged here");
                return;
            }

            services::TerrainMaterialLayerPatch patch;
            patch.name = "Never Saved";
            const auto edit = editLayer(created.path, std::nullopt, patch);
            CHECK_FALSE(edit.success);
            CHECK(contains(edit.error, "the material is unchanged"));

            CHECK(cached->activeLayerCount == 1);
            CHECK(cached->layers[1].name.empty());
            CHECK(notifications.assetSaved == 0);
            CHECK(notifications.materialCompiled == 0);
        }

        // Writable again: the same edit goes through.
        services::TerrainMaterialLayerPatch patch;
        patch.name = "Saved";
        const auto edit = editLayer(created.path, std::nullopt, patch);
        REQUIRE(edit.success);
        CHECK(cached->activeLayerCount == 2);
        CHECK(cached->layers[1].name == "Saved");
    }

    TEST_CASE("terrain_authoring: SetTerrainMaterialPath marks the terrain unsaved only when the reference changes")
    {
        AuthoringFixture fixture;
        ScopedTempDirectory dir("material-dirty");

        const auto first = createMaterial(dir.file("terrains"), "First");
        const auto second = createMaterial(dir.file("terrains"), "Second");
        REQUIRE(first.success);
        REQUIRE(second.success);

        const services::EntityHandle t1 = fixture.service.createTerrain(twoByTwo());
        REQUIRE(t1.isValid());
        ScopedTerrain guard1(fixture.service, t1);
        CHECK_FALSE(fixture.service.getTerrainData(t1)->saveDirty);

        setTerrainMaterial(t1, first.path);
        auto data = fixture.service.getTerrainData(t1);
        REQUIRE(data.has_value());
        CHECK(data->saveDirty);
        CHECK(sameFile(data->terrainMaterialPath, first.path));

        const std::string path = dir.file("terrain.vfTerrain");
        REQUIRE(saveTerrains({{t1, path}})[0].success);
        CHECK_FALSE(fixture.service.getTerrainData(t1)->saveDirty);

        // The same reference again is not a change.
        setTerrainMaterial(t1, first.path);
        CHECK_FALSE(fixture.service.getTerrainData(t1)->saveDirty);

        setTerrainMaterial(t1, second.path);
        CHECK(fixture.service.getTerrainData(t1)->saveDirty);

        REQUIRE(saveTerrains({{t1, path}})[0].success);
        CHECK_FALSE(fixture.service.getTerrainData(t1)->saveDirty);

        // And it is the header that carries it: a reload comes back with the second material.
        const services::EntityHandle loaded = loadTerrain(path);
        REQUIRE(loaded.isValid());
        ScopedTerrain guardLoaded(fixture.service, loaded);
        const auto loadedData = fixture.service.getTerrainData(loaded);
        REQUIRE(loadedData.has_value());
        CHECK(sameFile(loadedData->terrainMaterialPath, second.path));
    }
}
