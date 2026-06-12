#pragma once
#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace gameExport
{
	struct ExportConfig
	{
		std::string gameName;
		std::string gameVersion;
		std::filesystem::path outputDirectory;
		std::filesystem::path projectFile;
		std::filesystem::path workingDirectory;
		std::string startupScene;
		std::string iconPath;    // .ico file path for exe icon embedding
		bool cleanBuild = false;
		bool verifyIntegrity = true;

		// Graph-authored materials (PBROutput node present) with no cached shader
		// render with the wrong look in shipped builds (runtime has no shaderc to
		// recover). When true such materials fail the export; the editor-side
		// pre-export pass normally recompiles them before this is ever hit.
		bool failOnEmptyMaterialShaders = true;

		// When true, assets the dependency graph cannot reach from any scene are
		// left out of the archive. Default off: mType scripts load assets by raw
		// path strings the graph cannot see — review the unreferenced-asset
		// report and extend alwaysIncludePatterns before enabling.
		bool stripUnreferencedAssets = false;

		// Extra ship-regardless globs on top of the built-in safety rules
		// (scripts/**, *.vfSettings, *.vfmeta, navmesh, input mappings, fonts).
		// Matched against project-relative forward-slash paths; a pattern
		// without '/' matches the filename only.
		std::vector<std::string> alwaysIncludePatterns;

		bool isValid() const
		{
			return !gameName.empty() &&
				   !gameVersion.empty() &&
				   !outputDirectory.empty() &&
				   !workingDirectory.empty() &&
				   !startupScene.empty();
		}
	};

	struct UnreferencedAsset
	{
		std::string path;          // project-relative forward-slash path
		uint64_t sizeBytes = 0;
	};

	struct ExportResult
	{
		bool success = false;
		std::string errorMessage;
		std::vector<std::string> warnings;
		std::filesystem::path outputPath;
		std::vector<std::string> brokenMaterials;
		std::vector<UnreferencedAsset> unreferencedAssets;
	};

	using ExportProgressCallback = std::function<void(float progress, const std::string& status)>;
}
