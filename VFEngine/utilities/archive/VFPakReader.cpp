#include "VFPakReader.hpp"
#include "../print/Log.hpp"

#include <lz4.h>
#include <fstream>
#include <limits>

namespace archive
{
	bool VFPakReader::open(const std::filesystem::path& archivePath)
	{
		entries.clear();
		hashToIndices.clear();
		archiveSize = 0;
		filePath = archivePath;

		std::ifstream file(archivePath, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			vfLogError("VFPakReader: Failed to open archive: {}", archivePath.string());
			return false;
		}

		const auto endPosition = file.tellg();
		if (endPosition < 0)
		{
			vfLogError("VFPakReader: Failed to determine archive size: {}", archivePath.string());
			return false;
		}
		archiveSize = static_cast<uint64_t>(endPosition);
		if (archiveSize < sizeof(VFPakHeader))
		{
			vfLogError("VFPakReader: Archive is too small: {}", archivePath.string());
			return false;
		}
		file.seekg(0);

		// Read header
		VFPakHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!file.good())
		{
			vfLogError("VFPakReader: Failed to read header: {}", archivePath.string());
			return false;
		}

		if (header.magic != VFPAK_MAGIC)
		{
			vfLogError("VFPakReader: Invalid magic number in {}", archivePath.string());
			return false;
		}

		if (header.version != VFPAK_VERSION)
		{
			vfLogError("VFPakReader: Unsupported version {} in {}", header.version, archivePath.string());
			return false;
		}

		if (header.tocOffset > archiveSize || header.tocSize > archiveSize - header.tocOffset ||
			header.entryCount > header.tocSize / 36u)
		{
			vfLogError("VFPakReader: Invalid TOC bounds in {}", archivePath.string());
			return false;
		}
		const uint64_t tocEnd = header.tocOffset + header.tocSize;

		// Seek to TOC
		file.seekg(static_cast<std::streamoff>(header.tocOffset));
		if (!file.good())
		{
			vfLogError("VFPakReader: Failed to seek to TOC in {}", archivePath.string());
			return false;
		}

		// Read TOC entries
		entries.reserve(header.entryCount);
		hashToIndices.reserve(header.entryCount);

		for (uint32_t i = 0; i < header.entryCount; ++i)
		{
			const auto entryStart = file.tellg();
			if (entryStart < 0 || static_cast<uint64_t>(entryStart) > tocEnd ||
				tocEnd - static_cast<uint64_t>(entryStart) < 36u)
			{
				vfLogError("VFPakReader: Truncated TOC entry {} in {}", i, archivePath.string());
				return false;
			}

			VFPakEntry entry{};

			file.read(reinterpret_cast<char*>(&entry.pathHash), sizeof(entry.pathHash));
			file.read(reinterpret_cast<char*>(&entry.dataOffset), sizeof(entry.dataOffset));
			file.read(reinterpret_cast<char*>(&entry.compressedSize), sizeof(entry.compressedSize));
			file.read(reinterpret_cast<char*>(&entry.uncompressedSize), sizeof(entry.uncompressedSize));

			uint8_t compType = 0;
			file.read(reinterpret_cast<char*>(&compType), sizeof(compType));
			if (compType > static_cast<uint8_t>(CompressionType::LZ4))
			{
				vfLogError("VFPakReader: Unknown compression type in entry {}", i);
				return false;
			}
			entry.compressionType = static_cast<CompressionType>(compType);

			file.read(reinterpret_cast<char*>(&entry.flags), sizeof(entry.flags));

			uint16_t pathLen = 0;
			file.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));

			const auto pathStart = file.tellg();
			if (!file.good() || pathLen > 4096 || pathStart < 0 ||
				static_cast<uint64_t>(pathStart) > tocEnd ||
				pathLen > tocEnd - static_cast<uint64_t>(pathStart))
			{
				vfLogError("VFPakReader: Corrupt TOC entry {} in {}", i, archivePath.string());
				return false;
			}

			entry.path.resize(pathLen);
			file.read(entry.path.data(), pathLen);
			if (!file.good() || entry.pathHash != hashPath(entry.path))
			{
				vfLogError("VFPakReader: Invalid path hash in TOC entry {}", i);
				return false;
			}

			const uint64_t storedSize = entry.compressedSize;
			if (entry.compressionType == CompressionType::None &&
				entry.compressedSize != entry.uncompressedSize)
			{
				vfLogError("VFPakReader: Invalid uncompressed size metadata for {}", entry.path);
				return false;
			}
			if (entry.compressionType == CompressionType::LZ4 &&
				(entry.compressedSize > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
				 entry.uncompressedSize > static_cast<uint64_t>(std::numeric_limits<int>::max())))
			{
				vfLogError("VFPakReader: LZ4 entry is too large: {}", entry.path);
				return false;
			}
			if (entry.dataOffset > header.tocOffset || storedSize > header.tocOffset - entry.dataOffset)
			{
				vfLogError("VFPakReader: Entry data is outside archive bounds: {}", entry.path);
				return false;
			}

			hashToIndices[entry.pathHash].push_back(entries.size());
			entries.push_back(std::move(entry));
		}

		vfLogInfo("VFPakReader: Loaded archive with {} entries from {}",
		          entries.size(), archivePath.string());

		return true;
	}

	void VFPakReader::close()
	{
		std::lock_guard<std::mutex> lock(streamMutex);
		if (sharedStream.is_open())
			sharedStream.close();
		entries.clear();
		hashToIndices.clear();
		filePath.clear();
		archiveSize = 0;
	}

	bool VFPakReader::contains(const std::string& path) const
	{
		return findEntry(path) != nullptr;
	}

	std::vector<uint8_t> VFPakReader::readEntry(const std::string& path) const
	{
		const VFPakEntry* entry = findEntry(path);
		if (!entry)
		{
			return {};
		}

		if (entry->compressedSize == 0 && entry->uncompressedSize == 0)
		{
			return {};
		}

		std::lock_guard<std::mutex> lock(streamMutex);
		if (!sharedStream.is_open())
		{
			sharedStream.open(filePath, std::ios::binary);
			if (!sharedStream.is_open())
			{
				vfLogError("VFPakReader: Failed to open archive for reading: {}", path);
				return {};
			}
		}

		sharedStream.seekg(static_cast<std::streamoff>(entry->dataOffset));
		auto& file = sharedStream;

		if (entry->compressionType == CompressionType::LZ4)
		{
			// Read compressed data
			std::vector<char> compressed(entry->compressedSize);
			file.read(compressed.data(), static_cast<std::streamsize>(entry->compressedSize));

			// Decompress
			std::vector<uint8_t> decompressed(entry->uncompressedSize);
			int result = LZ4_decompress_safe(
				compressed.data(),
				reinterpret_cast<char*>(decompressed.data()),
				static_cast<int>(entry->compressedSize),
				static_cast<int>(entry->uncompressedSize));

			if (result < 0 || static_cast<uint64_t>(result) != entry->uncompressedSize)
			{
				vfLogError("VFPakReader: LZ4 decompression failed for {}", path);
				return {};
			}

			return decompressed;
		}

		// Uncompressed
		std::vector<uint8_t> data(entry->uncompressedSize);
		file.read(reinterpret_cast<char*>(data.data()),
		          static_cast<std::streamsize>(entry->uncompressedSize));

		return data;
	}

	std::optional<VFPakReader::EntryLocation> VFPakReader::getEntryLocation(const std::string& path) const
	{
		const VFPakEntry* entry = findEntry(path);
		if (!entry)
		{
			return std::nullopt;
		}

		if (entry->compressionType != CompressionType::None)
		{
			vfLogWarning("VFPakReader: Cannot get raw location for compressed entry: {}", path);
			return std::nullopt;
		}
		if (entry->compressedSize != entry->uncompressedSize ||
			entry->dataOffset > archiveSize || entry->uncompressedSize > archiveSize - entry->dataOffset)
		{
			vfLogError("VFPakReader: Invalid raw location for entry: {}", path);
			return std::nullopt;
		}

		return EntryLocation{entry->dataOffset, entry->uncompressedSize};
	}

	const VFPakEntry* VFPakReader::getEntry(const std::string& path) const
	{
		return findEntry(path);
	}

	const VFPakEntry* VFPakReader::findEntry(const std::string& path) const
	{
		uint64_t hash = hashPath(path);
		auto it = hashToIndices.find(hash);
		if (it == hashToIndices.end())
		{
			return nullptr;
		}

		// Handle hash collisions by checking full path
		for (size_t idx : it->second)
		{
			if (entries[idx].path == path)
			{
				return &entries[idx];
			}
		}

		return nullptr;
	}
}
