#pragma once
#include "GameExportExport.hpp"
#include "ExportConfig.hpp"
#include "ExportManifest.hpp"

namespace gameExport
{
	#pragma warning(push)
	#pragma warning(disable: 4251)
	class VF_GAMEEXPORT_API GameExporter
	{
	public:
		ExportResult exportGame(const ExportConfig& config, ExportProgressCallback progressCallback = nullptr);

		// VK-1696: turns the game's fresh copy of Runtime.exe into the game's own exe: removes the
		// engine's Authenticode signature (a release package ships a signed Runtime.exe; the game's
		// developer signs the game) and replaces "VertexForge Runtime" in its version resource with the
		// game's name, version and publisher (ExeVersionInfo::gameFields). Runs before any other
		// resource edit (the icon). False with result.errorMessage set when either step fails; a
		// version text without a numeric prefix only adds a warning.
		static bool prepareGameExecutable(const std::filesystem::path& exePath,
										  const ExportConfig& config,
										  ExportResult& result);

	private:
		ExportManifest previousManifest;
		ExportManifest newManifest;
		bool validatePrerequisites(const ExportConfig& config, ExportResult& result);
		bool createOutputStructure(const ExportConfig& config, ExportResult& result);
		bool copyRuntimeExecutable(const ExportConfig& config, ExportResult& result);
		bool copyRuntimeDependencies(const ExportConfig& config, ExportResult& result);
		bool compileShaders(const ExportConfig& config, ExportResult& result);
		bool compileMaterialShaders(const ExportConfig& config, ExportResult& result);
		bool packAssets(const ExportConfig& config, ExportResult& result);
		bool copyPlugins(const ExportConfig& config, ExportResult& result);
		bool generateProjectConfig(const ExportConfig& config, ExportResult& result);
		bool embedExeIcon(const ExportConfig& config, ExportResult& result);
		bool validateExportedBuild(const ExportConfig& config, ExportResult& result);

		std::filesystem::path findRuntimeExe() const;
		std::filesystem::path findShaderDirectory() const;
		std::filesystem::path findIBLDirectory() const;
		std::filesystem::path findFontsDirectory() const;

		void copyDirectoryRecursive(const std::filesystem::path& src,
									const std::filesystem::path& dst,
									ExportResult& result) const;

		// Copies directory but skips .mt/.vfproj source files and the export output directory
		void copyDirectoryFilteredRecursive(const std::filesystem::path& src,
											const std::filesystem::path& dst,
											ExportResult& result,
											const std::filesystem::path& excludeDir = {}) const;
	};
	#pragma warning(pop)
}
