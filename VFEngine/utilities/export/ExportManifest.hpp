#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cstdint>

namespace gameExport
{
	struct ManifestSource
	{
		std::string path;
		int64_t modifiedTime = 0;
		uint64_t contentHash = 0;
	};

	struct ManifestEntry
	{
		std::string archivePath;
		uint64_t contentHash = 0;
		uint64_t uncompressedSize = 0;
		std::string sourceType;
		std::vector<ManifestSource> sources;
	};

	class ExportManifest
	{
	public:
		bool load(const std::filesystem::path& manifestPath);
		bool save(const std::filesystem::path& manifestPath) const;

		void addEntry(ManifestEntry entry);

		// Returns true if the source files have changed compared to the previous manifest
		bool hasSourceChanged(const std::string& archivePath,
							  const std::vector<ManifestSource>& currentSources) const;

		const std::vector<ManifestEntry>& getEntries() const { return entries; }
		const ManifestEntry* findEntry(const std::string& archivePath) const;

		std::string gameName;
		std::string gameVersion;
		std::string exportTimestamp;

	private:
		std::vector<ManifestEntry> entries;
		std::unordered_map<std::string, size_t> archivePathIndex;
	};

	// Hash a file's contents using FNV-1a
	uint64_t hashFile(const std::filesystem::path& filePath);

	// Get file modification time as epoch seconds
	int64_t getFileModifiedTime(const std::filesystem::path& filePath);

	// Format a uint64_t hash as "0x..." hex string
	std::string formatHash(uint64_t hash);

	// Parse a "0x..." hex string back to uint64_t
	uint64_t parseHash(const std::string& str);
}
