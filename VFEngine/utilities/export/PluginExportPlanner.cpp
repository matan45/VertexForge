#include "PluginExportPlanner.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>

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
			descriptor.version = j.value("version", "");
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

	PluginExportPlan planPluginExport(const std::vector<ParsedPluginDescriptor>& descriptors,
									  const std::map<std::string, bool>& overrides,
									  uint32_t expectedApiVersion)
	{
		PluginExportPlan plan;

		std::map<std::string, const ParsedPluginDescriptor*> byName;
		for (const auto& descriptor : descriptors)
		{
			byName[descriptor.name] = &descriptor;
		}

		auto isEnabled = [&](const ParsedPluginDescriptor& descriptor)
		{
			auto it = overrides.find(descriptor.name);
			return it != overrides.end() ? it->second : descriptor.enabled;
		};

		// Seed with enabled plugins, then pull dependencies in transitively
		std::map<std::string, bool> selected; // name -> selected as dependency only
		std::vector<std::string> pending;
		for (const auto& descriptor : descriptors)
		{
			if (isEnabled(descriptor))
			{
				selected[descriptor.name] = false;
				pending.push_back(descriptor.name);
			}
		}

		while (!pending.empty())
		{
			std::string current = pending.back();
			pending.pop_back();

			const auto* descriptor = byName[current];
			for (const auto& dep : descriptor->dependencies)
			{
				auto depIt = byName.find(dep);
				if (depIt == byName.end())
				{
					plan.errors.push_back("Plugin '" + current + "' depends on unknown plugin '" + dep + "'");
					continue;
				}

				if (selected.count(dep) == 0)
				{
					if (!isEnabled(*depIt->second))
					{
						plan.warnings.push_back("Plugin '" + dep + "' is disabled but ships anyway: '" +
							current + "' depends on it");
					}
					selected[dep] = true;
					pending.push_back(dep);
				}
			}
		}

		// Keep the caller's descriptor order for deterministic output
		for (const auto& descriptor : descriptors)
		{
			if (selected.count(descriptor.name) == 0) continue;

			if (expectedApiVersion != 0 && descriptor.apiVersion != expectedApiVersion)
			{
				plan.errors.push_back("Plugin '" + descriptor.name + "' targets API v" +
					std::to_string(descriptor.apiVersion) + " but the engine expects v" +
					std::to_string(expectedApiVersion) + " — rebuild the plugin");
				continue;
			}

			plan.pluginsToShip.push_back(descriptor);
		}

		return plan;
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
