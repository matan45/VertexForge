#include "PluginExportPlanner.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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

		// Folder names compare as NTFS compares them: ordinal, ignoring case (also beyond ASCII).
		bool sameFolderName(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
										b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
		}

		std::string utf8(const fs::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}
	}

	std::vector<ParsedPluginDescriptor> descriptorsFromHost(const std::vector<ExportPluginSource>& sources,
															std::vector<std::string>& warnings)
	{
		std::vector<ParsedPluginDescriptor> descriptors;
		for (const auto& source : sources)
		{
			if (source.descriptorPath.empty())
			{
				continue;
			}
			std::optional<ParsedPluginDescriptor> descriptor = parsePluginDescriptor(source.descriptorPath);
			if (!descriptor)
			{
				warnings.push_back("Plugin descriptor " + utf8(source.descriptorPath) +
					" could not be read: that plugin is not shipped");
				continue;
			}
			descriptor->enabled = source.enabled;
			descriptors.push_back(std::move(*descriptor));
		}
		return descriptors;
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

		// VK-1733: the game gets plugins/<descriptor's folder name>/ per plugin, so two shipped plugins
		// from folders of one name (a shipped plugin and a user's own, say) would overwrite each other.
		std::vector<std::pair<std::wstring, const ParsedPluginDescriptor*>> folders;
		for (const auto& descriptor : plan.pluginsToShip)
		{
			if (descriptor.descriptorPath.empty())
			{
				continue;
			}
			const fs::path folder = descriptor.descriptorPath.parent_path().filename();
			const std::wstring folderName = folder.wstring();
			const auto clash = std::find_if(folders.begin(), folders.end(), [&folderName](const auto& seen)
			{
				return sameFolderName(seen.first, folderName);
			});
			if (clash != folders.end())
			{
				const ParsedPluginDescriptor& first = *clash->second;
				plan.errors.push_back("Plugins '" + first.name + "' (" + utf8(first.descriptorPath) + ") and '" +
					descriptor.name + "' (" + utf8(descriptor.descriptorPath) + ") would both ship as plugins/" +
					utf8(folder) + " — rename one plugin's folder");
				continue;
			}
			folders.emplace_back(folderName, &descriptor);
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
