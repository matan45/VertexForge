#pragma once
#include "ExportConfig.hpp"

namespace gameExport
{
	class GameExporter
	{
	public:
		ExportResult exportGame(const ExportConfig& config, ExportProgressCallback progressCallback = nullptr);

	private:
		bool validatePrerequisites(const ExportConfig& config, ExportResult& result);
		bool createOutputStructure(const ExportConfig& config, ExportResult& result);
		bool copyRuntimeExecutable(const ExportConfig& config, ExportResult& result);
		bool copyRuntimeDependencies(const ExportConfig& config, ExportResult& result);
		bool copyShaders(const ExportConfig& config, ExportResult& result);
		bool copyAssets(const ExportConfig& config, ExportResult& result);
		bool copyPlugins(const ExportConfig& config, ExportResult& result);
		bool generateProjectConfig(const ExportConfig& config, ExportResult& result);
		bool embedExeIcon(const ExportConfig& config, ExportResult& result);
		bool validateExportedBuild(const ExportConfig& config, ExportResult& result);

		std::filesystem::path findRuntimeExe() const;
		std::filesystem::path findShaderDirectory() const;
		std::filesystem::path findResourcesEditorDirectory() const;

		void copyDirectoryRecursive(const std::filesystem::path& src,
									const std::filesystem::path& dst,
									ExportResult& result) const;
	};
}
