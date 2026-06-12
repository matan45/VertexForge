#include <doctest.h>
#include <export/PluginExportPlanner.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// ============================================================
// Export Pipeline v2: plugin ship filter — plugin folders are
// development workspaces (source, premake5.lua, PDBs); only the
// descriptor, the descriptor's DLL and runtime data folders may
// ship with an exported game.
// ============================================================

namespace
{
	namespace fs = std::filesystem;

	struct TempDir
	{
		fs::path root;

		TempDir(const std::string& name)
		{
			root = fs::temp_directory_path() / name;
			std::error_code ec;
			fs::remove_all(root, ec);
			fs::create_directories(root, ec);
		}

		~TempDir()
		{
			std::error_code ec;
			fs::remove_all(root, ec);
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

TEST_SUITE("ExportPluginFilter")
{

TEST_CASE("shouldShipPluginFile: ships descriptor and library DLL")
{
	CHECK(gameExport::shouldShipPluginFile("HexTerrain.vfplugin", "HexTerrain.dll"));
	CHECK(gameExport::shouldShipPluginFile("HexTerrain.dll", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: library match is case-insensitive")
{
	CHECK(gameExport::shouldShipPluginFile("hexterrain.DLL", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: never ships source, build scripts or debug info")
{
	CHECK_FALSE(gameExport::shouldShipPluginFile("HexTerrain.cpp", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile("HexTerrain.hpp", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile("premake5.lua", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile("HexTerrain.pdb", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile("HexTerrain.obj", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile("README.md", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: does not ship unrelated DLLs")
{
	CHECK_FALSE(gameExport::shouldShipPluginFile("SomeOther.dll", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: ships assets and resources subfolders wholesale")
{
	CHECK(gameExport::shouldShipPluginFile(fs::path("assets") / "icon.vfImage", "HexTerrain.dll"));
	CHECK(gameExport::shouldShipPluginFile(fs::path("resources") / "shaders" / "hex.glsl", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: skips nested non-data folders")
{
	CHECK_FALSE(gameExport::shouldShipPluginFile(fs::path("src") / "impl.cpp", "HexTerrain.dll"));
	CHECK_FALSE(gameExport::shouldShipPluginFile(fs::path("obj") / "HexTerrain.dll", "HexTerrain.dll"));
}

TEST_CASE("shouldShipPluginFile: empty path is rejected")
{
	CHECK_FALSE(gameExport::shouldShipPluginFile(fs::path{}, "HexTerrain.dll"));
}

TEST_CASE("parsePluginDescriptor: reads the export-relevant fields")
{
	TempDir dir("vf_export_plugin_descriptor");
	auto path = dir.writeFile("Sample.vfplugin", R"({
		"name": "Sample",
		"library": "Sample.dll",
		"enabled": false,
		"apiVersion": 12,
		"dependencies": ["Base", "Other"]
	})");

	auto descriptor = gameExport::parsePluginDescriptor(path);
	REQUIRE(descriptor.has_value());
	CHECK(descriptor->name == "Sample");
	CHECK(descriptor->library == "Sample.dll");
	CHECK_FALSE(descriptor->enabled);
	CHECK(descriptor->apiVersion == 12);
	REQUIRE(descriptor->dependencies.size() == 2);
	CHECK(descriptor->dependencies[0] == "Base");
	CHECK(descriptor->dependencies[1] == "Other");
}

TEST_CASE("parsePluginDescriptor: defaults enabled to true and name to file stem")
{
	TempDir dir("vf_export_plugin_descriptor_defaults");
	auto path = dir.writeFile("Minimal.vfplugin", R"({ "library": "Minimal.dll" })");

	auto descriptor = gameExport::parsePluginDescriptor(path);
	REQUIRE(descriptor.has_value());
	CHECK(descriptor->name == "Minimal");
	CHECK(descriptor->enabled);
	CHECK(descriptor->apiVersion == 0);
	CHECK(descriptor->dependencies.empty());
}

TEST_CASE("parsePluginDescriptor: rejects missing library field")
{
	TempDir dir("vf_export_plugin_descriptor_nolib");
	auto path = dir.writeFile("Broken.vfplugin", R"({ "name": "Broken" })");

	CHECK_FALSE(gameExport::parsePluginDescriptor(path).has_value());
}

TEST_CASE("parsePluginDescriptor: rejects malformed JSON and missing file")
{
	TempDir dir("vf_export_plugin_descriptor_badjson");
	auto path = dir.writeFile("Bad.vfplugin", "{ not json");

	CHECK_FALSE(gameExport::parsePluginDescriptor(path).has_value());
	CHECK_FALSE(gameExport::parsePluginDescriptor(dir.root / "Missing.vfplugin").has_value());
}

// ---- planPluginExport ----

namespace
{
	gameExport::ParsedPluginDescriptor makeDescriptor(const std::string& name, bool enabled,
													  uint32_t apiVersion = 12,
													  std::vector<std::string> dependencies = {})
	{
		gameExport::ParsedPluginDescriptor descriptor;
		descriptor.name = name;
		descriptor.library = name + ".dll";
		descriptor.enabled = enabled;
		descriptor.apiVersion = apiVersion;
		descriptor.dependencies = std::move(dependencies);
		return descriptor;
	}

	bool plansToShip(const gameExport::PluginExportPlan& plan, const std::string& name)
	{
		for (const auto& descriptor : plan.pluginsToShip)
		{
			if (descriptor.name == name) return true;
		}
		return false;
	}
}

TEST_CASE("planPluginExport: ships enabled, skips disabled")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true), makeDescriptor("B", false)}, {}, 12);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
	CHECK_FALSE(plansToShip(plan, "B"));
}

TEST_CASE("planPluginExport: overrides beat the descriptor's enabled flag both ways")
{
	std::map<std::string, bool> overrides = {{"A", false}, {"B", true}};
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true), makeDescriptor("B", false)}, overrides, 12);

	CHECK(plan.errors.empty());
	CHECK_FALSE(plansToShip(plan, "A"));
	CHECK(plansToShip(plan, "B"));
}

TEST_CASE("planPluginExport: dependencies pull in disabled plugins with a warning")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 12, {"Base"}), makeDescriptor("Base", false)}, {}, 12);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
	CHECK(plansToShip(plan, "Base"));
	REQUIRE(plan.warnings.size() == 1);
	CHECK(plan.warnings[0].find("Base") != std::string::npos);
}

TEST_CASE("planPluginExport: transitive dependency chains resolve")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 12, {"B"}),
		 makeDescriptor("B", false, 12, {"C"}),
		 makeDescriptor("C", false)}, {}, 12);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
	CHECK(plansToShip(plan, "B"));
	CHECK(plansToShip(plan, "C"));
}

TEST_CASE("planPluginExport: unknown dependency is an error")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 12, {"Ghost"})}, {}, 12);

	REQUIRE(plan.errors.size() == 1);
	CHECK(plan.errors[0].find("Ghost") != std::string::npos);
}

TEST_CASE("planPluginExport: API version mismatch on a shipped plugin is an error")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("Old", true, 11)}, {}, 12);

	REQUIRE(plan.errors.size() == 1);
	CHECK(plan.errors[0].find("Old") != std::string::npos);
	CHECK_FALSE(plansToShip(plan, "Old"));
}

TEST_CASE("planPluginExport: API version of excluded plugins is not checked")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 12), makeDescriptor("Stale", false, 9)}, {}, 12);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
}

TEST_CASE("planPluginExport: expectedApiVersion 0 skips the version check")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 11)}, {}, 0);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
}

TEST_CASE("planPluginExport: dependency cycles terminate")
{
	auto plan = gameExport::planPluginExport(
		{makeDescriptor("A", true, 12, {"B"}),
		 makeDescriptor("B", false, 12, {"A"})}, {}, 12);

	CHECK(plan.errors.empty());
	CHECK(plansToShip(plan, "A"));
	CHECK(plansToShip(plan, "B"));
}

} // TEST_SUITE
