#include "ExportManifest.hpp"
#include "../archive/VFPakFormat.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstdio>

namespace gameExport
{
	static constexpr int MANIFEST_VERSION = 1;

	std::string formatHash(uint64_t hash)
	{
		char buf[19];
		snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(hash));
		return std::string(buf);
	}

	uint64_t parseHash(const std::string& str)
	{
		if (str.size() > 2 && str[0] == '0' && str[1] == 'x')
		{
			return std::strtoull(str.c_str() + 2, nullptr, 16);
		}
		return std::strtoull(str.c_str(), nullptr, 16);
	}

	uint64_t hashFile(const std::filesystem::path& filePath)
	{
		std::ifstream file(filePath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) return 0;

		auto size = static_cast<size_t>(file.tellg());
		if (size == 0) return archive::hashBytes(nullptr, 0);

		file.seekg(0);
		std::vector<uint8_t> data(size);
		file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
		return archive::hashBytes(data.data(), data.size());
	}

	int64_t getFileModifiedTime(const std::filesystem::path& filePath)
	{
		std::error_code ec;
		auto ftime = std::filesystem::last_write_time(filePath, ec);
		if (ec) return 0;

		auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
		return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
	}

	bool ExportManifest::load(const std::filesystem::path& manifestPath)
	{
		std::ifstream file(manifestPath);
		if (!file.is_open()) return false;

		try
		{
			auto j = nlohmann::json::parse(file);
			if (j.value("version", 0) != MANIFEST_VERSION) return false;

			gameName = j.value("gameName", "");
			gameVersion = j.value("gameVersion", "");
			exportTimestamp = j.value("exportTimestamp", "");

			entries.clear();
			archivePathIndex.clear();

			for (const auto& je : j["entries"])
			{
				ManifestEntry entry;
				entry.archivePath = je.value("archivePath", "");
				entry.contentHash = parseHash(je.value("contentHash", "0"));
				entry.uncompressedSize = je.value("uncompressedSize", uint64_t(0));
				entry.sourceType = je.value("sourceType", "");

				for (const auto& js : je["sources"])
				{
					ManifestSource src;
					src.path = js.value("path", "");
					src.modifiedTime = js.value("modifiedTime", int64_t(0));
					src.contentHash = parseHash(js.value("contentHash", "0"));
					entry.sources.push_back(std::move(src));
				}

				archivePathIndex[entry.archivePath] = entries.size();
				entries.push_back(std::move(entry));
			}

			return true;
		}
		catch (const nlohmann::json::exception& e)
		{
			vfLogWarning("Failed to parse export manifest: {}", e.what());
			return false;
		}
	}

	bool ExportManifest::save(const std::filesystem::path& manifestPath) const
	{
		nlohmann::json j;
		j["version"] = MANIFEST_VERSION;
		j["gameName"] = gameName;
		j["gameVersion"] = gameVersion;
		j["exportTimestamp"] = exportTimestamp;

		auto& jEntries = j["entries"] = nlohmann::json::array();
		for (const auto& entry : entries)
		{
			nlohmann::json je;
			je["archivePath"] = entry.archivePath;
			je["contentHash"] = formatHash(entry.contentHash);
			je["uncompressedSize"] = entry.uncompressedSize;
			je["sourceType"] = entry.sourceType;

			auto& jSources = je["sources"] = nlohmann::json::array();
			for (const auto& src : entry.sources)
			{
				nlohmann::json js;
				js["path"] = src.path;
				js["modifiedTime"] = src.modifiedTime;
				js["contentHash"] = formatHash(src.contentHash);
				jSources.push_back(std::move(js));
			}
			jEntries.push_back(std::move(je));
		}

		std::ofstream file(manifestPath);
		if (!file.is_open())
		{
			vfLogError("Failed to write export manifest: {}", manifestPath.string());
			return false;
		}

		file << j.dump(2);
		return file.good();
	}

	void ExportManifest::addEntry(ManifestEntry entry)
	{
		auto it = archivePathIndex.find(entry.archivePath);
		if (it != archivePathIndex.end())
		{
			entries[it->second] = std::move(entry);
		}
		else
		{
			archivePathIndex[entry.archivePath] = entries.size();
			entries.push_back(std::move(entry));
		}
	}

	bool ExportManifest::hasSourceChanged(const std::string& archivePath,
										  const std::vector<ManifestSource>& currentSources) const
	{
		const auto* prev = findEntry(archivePath);
		if (!prev) return true;

		if (prev->sources.size() != currentSources.size()) return true;

		for (const auto& current : currentSources)
		{
			bool found = false;
			for (const auto& prevSrc : prev->sources)
			{
				if (prevSrc.path == current.path)
				{
					found = true;
					// Fast path: if mtime unchanged, skip hash check
					if (prevSrc.modifiedTime == current.modifiedTime) break;
					// Slow path: compare content hash
					if (prevSrc.contentHash != current.contentHash) return true;
					break;
				}
			}
			if (!found) return true;
		}

		return false;
	}

	const ManifestEntry* ExportManifest::findEntry(const std::string& archivePath) const
	{
		auto it = archivePathIndex.find(archivePath);
		if (it == archivePathIndex.end()) return nullptr;
		return &entries[it->second];
	}
}
