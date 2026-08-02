#pragma once

#include "VFPakFormat.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>

namespace archive
{
	class VFPakReader
	{
	public:
		bool open(const std::filesystem::path& archivePath);
		void close();

		bool contains(const std::string& path) const;

		// Read and decompress an entire entry into memory
		std::vector<uint8_t> readEntry(const std::string& path) const;

		// Get raw location in archive for uncompressed streaming entries
		struct EntryLocation
		{
			uint64_t offset;
			uint64_t size;
		};
		std::optional<EntryLocation> getEntryLocation(const std::string& path) const;

		// Get the entry metadata
		const VFPakEntry* getEntry(const std::string& path) const;

		const std::filesystem::path& getArchivePath() const { return filePath; }
		uint32_t getEntryCount() const { return static_cast<uint32_t>(entries.size()); }

	private:
		std::filesystem::path filePath;
		uint64_t archiveSize = 0;
		std::unordered_map<uint64_t, std::vector<size_t>> hashToIndices;
		std::vector<VFPakEntry> entries;

		mutable std::ifstream sharedStream;
		mutable std::mutex streamMutex;

		const VFPakEntry* findEntry(const std::string& path) const;
	};
}
