#pragma once
#include "ExportConfig.hpp"
#include "../config/ProjectConfig.hpp"
#include "../launch/GameStateLocation.hpp"
#include <filesystem>
#include <string>

namespace gameExport
{
	// The .vfproj the exporter writes beside the shipped game (GameExporter::generateProjectConfig). Header-only
	// so the tests can check it without running an export.
	//
	// VK-1726: it always says where the game keeps its config, saves, logs and crash reports: "stateLocation" is
	// "portable" for a portable build (beside the .vfproj) and "user" otherwise (%LOCALAPPDATA%\<publisher>\<game>),
	// with the project's publisher. Runtime.exe reads both before its log opens (launch/GameStateLocation.hpp).
	inline config::ProjectConfig shippedProjectConfig(const ExportConfig& config)
	{
		config::ProjectConfig project;
		project.projectName = config.gameName;
		project.version = config.gameVersion;
		project.workingDirectory = "Assets";
		project.startupScene = config.startupScene;
		project.fontFallbackChain = config.fontFallbackChain;
		// VK-1687: an icon outside the project is kept absolute in the editor's .vfproj; the shipped
		// game never gets the developer's absolute path, only the file name (as it did before).
		const std::filesystem::path iconPath(config.iconPath);
		project.exeIconPath = iconPath.is_absolute() ? iconPath.filename().string() : config.iconPath;
		if (config.expectedPluginApiVersion != 0)
		{
			project.pluginApiVersion = config.expectedPluginApiVersion;
		}
		project.publisher = config.publisher;
		project.stateLocation = std::string(config.portableBuild ? launch::gameStateLocationPortable
																 : launch::gameStateLocationUser);
		return project;
	}
}
