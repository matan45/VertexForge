#pragma once
#include "GameExportExport.hpp"
#include "../asset/AssetGUID.hpp"
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace gameExport
{
	#pragma warning(push)
	#pragma warning(disable: 4251)

	// Set of assets reachable from the project's scenes through the
	// AssetDatabase dependency graph. Files without a database entry are never
	// classified as unreferenced — only tracked assets the graph cannot reach.
	struct VF_GAMEEXPORT_API AssetClosure
	{
		// False when no scene resolved to a database GUID (empty/stale database):
		// classification is skipped entirely rather than reporting everything.
		bool valid = false;
		std::unordered_set<asset::AssetGUID, asset::AssetGUID::Hash> referencedGuids;
	};

	class VF_GAMEEXPORT_API AssetClosureResolver
	{
	public:
		// Seeds the closure from every .vfScene under projectRoot (runtime
		// scene-switching must keep working, so all scenes count — not just the
		// startup scene) and BFS-walks AssetDatabase::getDependencies.
		// refreshScan re-runs DependencyScanner::scanAll first so the graph
		// reflects on-disk asset state instead of stale .vfmeta data.
		static AssetClosure resolve(const std::filesystem::path& projectRoot, bool refreshScan = true);

		// Glob match against a project-relative forward-slash path, case-insensitive.
		// A pattern without '/' matches the filename only ("*.vfmeta");
		// '**' spans path separators, '*'/'?' do not.
		static bool matchesPattern(const std::string& relativePath, const std::string& pattern);

		// True when the path matches a built-in safety rule (scripts, settings,
		// meta sidecars, navmesh, input mappings, fonts) or a user pattern.
		// These ship regardless of graph reachability — mType scripts load
		// assets by raw path strings the dependency graph cannot see.
		static bool isAlwaysIncluded(const std::string& relativePath,
									 const std::vector<std::string>& userPatterns);
	};

	#pragma warning(pop)
}
