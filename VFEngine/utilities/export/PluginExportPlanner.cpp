#include "PluginExportPlanner.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>

namespace gameExport
{
	namespace fs = std::filesystem;

	namespace
	{
		std::string toLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
						   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}
	}

	std::optional<ParsedPluginDescriptor> parsePluginDescriptor(const fs::path& descriptorPath)
	{
		std::ifstream file(descriptorPath);
		if (!file.is_open())
		{
			return std::nullopt;
		}

		try
		{
			auto j = nlohmann::json::parse(file);

			ParsedPluginDescriptor descriptor;
			descriptor.descriptorPath = descriptorPath;
			descriptor.name = j.value("name", descriptorPath.stem().string());
			descriptor.library = j.value("library", "");
			descriptor.enabled = j.value("enabled", true);
			descriptor.apiVersion = j.value("apiVersion", uint32_t(0));

			if (j.contains("dependencies") && j["dependencies"].is_array())
			{
				for (const auto& dep : j["dependencies"])
				{
					if (dep.is_string())
					{
						descriptor.dependencies.push_back(dep.get<std::string>());
					}
				}
			}

			if (descriptor.library.empty())
			{
				vfLogWarning("Plugin descriptor missing 'library' field: {}", descriptorPath.string());
				return std::nullopt;
			}

			return descriptor;
		}
		catch (const nlohmann::json::exception& e)
		{
			vfLogWarning("Failed to parse plugin descriptor {}: {}", descriptorPath.string(), e.what());
			return std::nullopt;
		}
	}

	bool shouldShipPluginFile(const fs::path& relativePath, const std::string& libraryFileName)
	{
		if (relativePath.empty())
		{
			return false;
		}

		// Runtime data folders ship wholesale
		std::string firstComponent = toLower(relativePath.begin()->string());
		if (firstComponent == "assets" || firstComponent == "resources")
		{
			return true;
		}

		// Only top-level descriptor + library DLL beyond that
		if (std::distance(relativePath.begin(), relativePath.end()) > 1)
		{
			return false;
		}

		std::string fileName = toLower(relativePath.filename().string());
		std::string extension = toLower(relativePath.extension().string());

		if (extension == ".vfplugin")
		{
			return true;
		}

		return fileName == toLower(libraryFileName);
	}
}
