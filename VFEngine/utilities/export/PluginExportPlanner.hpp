#pragma once
#include "GameExportExport.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gameExport
{
	// Parsed view of a <PluginName>.vfplugin descriptor — only the fields export
	// decisions need. Parsed locally with nlohmann so GameExport does not link
	// the Plugin module (the editor links Plugin; this DLL must not).
	struct ParsedPluginDescriptor
	{
		std::string name;
		std::string version;
		std::string library;           // DLL filename from the "library" field
		bool enabled = true;
		uint32_t apiVersion = 0;
		std::vector<std::string> dependencies;
		std::filesystem::path descriptorPath;
	};

	VF_GAMEEXPORT_API std::optional<ParsedPluginDescriptor>
	parsePluginDescriptor(const std::filesystem::path& descriptorPath);

	// Decides whether a file inside a plugin folder ships with the exported game.
	// relativePath is relative to the plugin's own folder (plugins/<Name>/).
	// Ships: the .vfplugin descriptor, the DLL named by the descriptor's
	// "library" field, and anything under assets/ or resources/.
	// Skips everything else — plugin folders also hold the plugin's source code,
	// premake script and PDBs, which must never leak into a shipped game.
	VF_GAMEEXPORT_API bool shouldShipPluginFile(const std::filesystem::path& relativePath,
												const std::string& libraryFileName);

	#pragma warning(push)
	#pragma warning(disable: 4251)
	struct VF_GAMEEXPORT_API PluginExportPlan
	{
		std::vector<ParsedPluginDescriptor> pluginsToShip;
		std::vector<std::string> errors;    // any entry fails the export
		std::vector<std::string> warnings;
	};
	#pragma warning(pop)

	// Pure selection logic: which plugins ship with the exported game.
	// - effective enabled state = overrides[name] when present, else the
	//   descriptor's global "enabled" flag
	// - descriptor dependencies are pulled in transitively (with a warning when
	//   that re-includes a disabled plugin); an unknown dependency is an error
	// - a shipped plugin whose apiVersion differs from expectedApiVersion is an
	//   error (skipped when expectedApiVersion is 0 — version unknown)
	VF_GAMEEXPORT_API PluginExportPlan planPluginExport(
		const std::vector<ParsedPluginDescriptor>& descriptors,
		const std::map<std::string, bool>& overrides,
		uint32_t expectedApiVersion);
}
