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

		bool isValid() const
		{
			return !gameName.empty() &&
				   !gameVersion.empty() &&
				   !outputDirectory.empty() &&
				   !workingDirectory.empty() &&
				   !startupScene.empty();
		}
	};

	struct ExportResult
	{
		bool success = false;
		std::string errorMessage;
		std::vector<std::string> warnings;
		std::filesystem::path outputPath;
		std::vector<std::string> brokenMaterials;
	};

	using ExportProgressCallback = std::function<void(float progress, const std::string& status)>;
}
