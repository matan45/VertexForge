#include <doctest.h>
#include <export/AssetClosureResolver.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// Export Pipeline v2: dependency-aware packing. The closure is
// seeded from every .vfScene and BFS-walks the AssetDatabase
// graph; always-include globs protect script-loaded paths the
// graph cannot see.
//
// AssetDatabase is a process-wide singleton, so each case
// clears it and builds its own temp project tree. Dependencies
// are set manually (resolve(refreshScan=false)) so the scanner
// does not overwrite them from file contents.
// ============================================================

namespace
{
	namespace fs = std::filesystem;

	struct TempProject
	{
		fs::path root;

		TempProject(const std::string& name)
		{
			root = fs::temp_directory_path() / name;
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root, ec);
			asset::AssetDatabase::instance().clear();
		}

		~TempProject()
		{
			std::error_code ec;
			fs::remove_all(root, ec);
			asset::AssetDatabase::instance().clear();
		}

		fs::path writeFile(const std::string& relPath, const std::string& content) const
		{
			fs::path p = root / relPath;
			std::error_code ec;
			fs::create_directories(p.parent_path(), ec);
			std::ofstream file(p);
			file << content;
			return p;
		}
	};
}

TEST_SUITE("ExportAssetClosure")
{

// ---- matchesPattern ----

TEST_CASE("matchesPattern: pattern without slash matches the filename only")
{
	CHECK(gameExport::AssetClosureResolver::matchesPattern("assets/textures/wood.vfmeta", "*.vfmeta"));
	CHECK(gameExport::AssetClosureResolver::matchesPattern("wood.vfmeta", "*.vfmeta"));
	CHECK_FALSE(gameExport::AssetClosureResolver::matchesPattern("assets/wood.vfimage", "*.vfmeta"));
}

TEST_CASE("matchesPattern: double star spans directories, single star does not")
{
	CHECK(gameExport::AssetClosureResolver::matchesPattern("assets/prefabs/units/tank.vfscene", "assets/prefabs/**"));
	CHECK(gameExport::AssetClosureResolver::matchesPattern("assets/prefabs/tank.vfscene", "assets/prefabs/**"));
	CHECK(gameExport::AssetClosureResolver::matchesPattern("assets/prefabs/tank.vfscene", "assets/*/tank.vfscene"));
	CHECK_FALSE(gameExport::AssetClosureResolver::matchesPattern("assets/prefabs/units/tank.vfscene", "assets/*/tank.vfscene"));
	CHECK(gameExport::AssetClosureResolver::matchesPattern("assets/prefabs/units/tank.vfscene", "assets/**/tank.vfscene"));
}

TEST_CASE("matchesPattern: question mark matches a single character")
{
	CHECK(gameExport::AssetClosureResolver::matchesPattern("lod1.vfmesh", "lod?.vfmesh"));
	CHECK_FALSE(gameExport::AssetClosureResolver::matchesPattern("lod12.vfmesh", "lod?.vfmesh"));
}

TEST_CASE("matchesPattern: case-insensitive")
{
	CHECK(gameExport::AssetClosureResolver::matchesPattern("Assets/UI/Theme.vfTheme", "assets/ui/*.vftheme"));
}

TEST_CASE("matchesPattern: empty pattern never matches")
{
	CHECK_FALSE(gameExport::AssetClosureResolver::matchesPattern("anything", ""));
}

// ---- isAlwaysIncluded ----

TEST_CASE("isAlwaysIncluded: built-in safety rules")
{
	std::vector<std::string> none;
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("scripts/compiled/scripts.mtcLib", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("scenes/Main.vfSettings", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("assets/textures/wood.vfmeta", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("navmesh/index.vfNavIndex", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("navmesh/tile_0_0.vfNavTile", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("config/input.vfInputMapping", none));
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("fonts/Roboto.ttf", none));

	CHECK_FALSE(gameExport::AssetClosureResolver::isAlwaysIncluded("assets/textures/wood.vfimage", none));
}

TEST_CASE("isAlwaysIncluded: user patterns extend the built-ins")
{
	std::vector<std::string> patterns = {"assets/prefabs/**"};
	CHECK(gameExport::AssetClosureResolver::isAlwaysIncluded("assets/prefabs/units/tank.vfscene", patterns));
	CHECK_FALSE(gameExport::AssetClosureResolver::isAlwaysIncluded("assets/textures/wood.vfimage", patterns));
}

// ---- resolve ----

TEST_CASE("resolve: empty database yields an invalid closure")
{
	TempProject proj("vf_export_closure_empty_db");
	proj.writeFile("scenes/Main.vfScene", "{}");

	auto closure = gameExport::AssetClosureResolver::resolve(proj.root, false);
	CHECK_FALSE(closure.valid);
}

TEST_CASE("resolve: no scene registered in the database yields an invalid closure")
{
	TempProject proj("vf_export_closure_no_scene_guid");
	auto& db = asset::AssetDatabase::instance();

	auto texPath = proj.writeFile("assets/wood.vfimage", "binary");
	db.registerAsset(texPath.string(), resource::AssetType::Texture);
	proj.writeFile("scenes/Main.vfScene", "{}"); // on disk but never registered

	auto closure = gameExport::AssetClosureResolver::resolve(proj.root, false);
	CHECK_FALSE(closure.valid);
}

TEST_CASE("resolve: walks the dependency chain from every scene")
{
	TempProject proj("vf_export_closure_chain");
	auto& db = asset::AssetDatabase::instance();

	auto scenePath = proj.writeFile("scenes/Main.vfScene", "{}");
	auto matPath = proj.writeFile("assets/wood.vfmat", "{}");
	auto texPath = proj.writeFile("assets/wood.vfimage", "binary");
	auto orphanPath = proj.writeFile("assets/orphan.vfimage", "binary");

	auto sceneGuid = db.registerAsset(scenePath.string(), resource::AssetType::Scene);
	auto matGuid = db.registerAsset(matPath.string(), resource::AssetType::Material);
	auto texGuid = db.registerAsset(texPath.string(), resource::AssetType::Texture);
	auto orphanGuid = db.registerAsset(orphanPath.string(), resource::AssetType::Texture);

	db.setDependencies(sceneGuid, {matGuid});
	db.setDependencies(matGuid, {texGuid});

	auto closure = gameExport::AssetClosureResolver::resolve(proj.root, false);
	REQUIRE(closure.valid);
	CHECK(closure.referencedGuids.count(sceneGuid) == 1);
	CHECK(closure.referencedGuids.count(matGuid) == 1);
	CHECK(closure.referencedGuids.count(texGuid) == 1);
	CHECK(closure.referencedGuids.count(orphanGuid) == 0);
}

TEST_CASE("resolve: assets reachable only from a secondary scene still count")
{
	TempProject proj("vf_export_closure_second_scene");
	auto& db = asset::AssetDatabase::instance();

	auto mainPath = proj.writeFile("scenes/Main.vfScene", "{}");
	auto otherPath = proj.writeFile("scenes/Level2.vfScene", "{}");
	auto texPath = proj.writeFile("assets/level2.vfimage", "binary");

	db.registerAsset(mainPath.string(), resource::AssetType::Scene);
	auto otherGuid = db.registerAsset(otherPath.string(), resource::AssetType::Scene);
	auto texGuid = db.registerAsset(texPath.string(), resource::AssetType::Texture);

	db.setDependencies(otherGuid, {texGuid});

	auto closure = gameExport::AssetClosureResolver::resolve(proj.root, false);
	REQUIRE(closure.valid);
	CHECK(closure.referencedGuids.count(texGuid) == 1);
}

TEST_CASE("resolve: dependency cycles terminate")
{
	TempProject proj("vf_export_closure_cycle");
	auto& db = asset::AssetDatabase::instance();

	auto scenePath = proj.writeFile("scenes/Main.vfScene", "{}");
	auto aPath = proj.writeFile("assets/a.vfmat", "{}");
	auto bPath = proj.writeFile("assets/b.vfmat", "{}");

	auto sceneGuid = db.registerAsset(scenePath.string(), resource::AssetType::Scene);
	auto aGuid = db.registerAsset(aPath.string(), resource::AssetType::Material);
	auto bGuid = db.registerAsset(bPath.string(), resource::AssetType::Material);

	db.setDependencies(sceneGuid, {aGuid});
	db.setDependencies(aGuid, {bGuid});
	db.setDependencies(bGuid, {aGuid});

	auto closure = gameExport::AssetClosureResolver::resolve(proj.root, false);
	REQUIRE(closure.valid);
	CHECK(closure.referencedGuids.count(aGuid) == 1);
	CHECK(closure.referencedGuids.count(bGuid) == 1);
}

} // TEST_SUITE
