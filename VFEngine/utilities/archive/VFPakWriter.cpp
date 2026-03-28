#include "VFPakWriter.hpp"
#include "print/Log.hpp"

#include <lz4.h>
#include <lz4hc.h>

namespace archive
{
	bool VFPakWriter::create(const std::filesystem::path& path)
	{
		outputPath = path;
		file.open(path, std::ios::binary);
		if (!file.is_open())
		{
			vfLogError("VFPakWriter: Failed to create archive: {}", path.string());
			return false;
		}

		// Write placeholder header (will be patched in finalize)
		VFPakHeader header{};
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		currentOffset = VFPAK_HEADER_SIZE;

		return true;
	}

	bool VFPakWriter::addFile(const std::string& archivePath,
	                           const std::filesystem::path& sourcePath,
	                           CompressionType compression)
	{
		std::ifstream sourceFile(sourcePath, std::ios::binary | std::ios::ate);
		if (!sourceFile.is_open())
		{
			vfLogWarning("VFPakWriter: Failed to open source file: {}", sourcePath.string());
			return false;
		}

		auto fileSize = static_cast<size_t>(sourceFile.tellg());
		if (fileSize == 0)
		{
			// Store empty files as-is
			VFPakEntry entry{};
			entry.pathHash = hashPath(archivePath);
			entry.path = archivePath;
			entry.dataOffset = currentOffset;
			entry.compressedSize = 0;
			entry.uncompressedSize = 0;
			entry.compressionType = CompressionType::None;
			entries.push_back(std::move(entry));
			return true;
		}

		sourceFile.seekg(0);
		std::vector<char> data(fileSize);
		sourceFile.read(data.data(), static_cast<std::streamsize>(fileSize));
		sourceFile.close();

		return addMemory(archivePath, data.data(), fileSize, compression);
	}

	bool VFPakWriter::addMemory(const std::string& archivePath,
	                             const void* data, size_t size,
	                             CompressionType compression)
	{
		VFPakEntry entry{};
		entry.pathHash = hashPath(archivePath);
		entry.path = archivePath;
		entry.uncompressedSize = size;

		if (compression == CompressionType::LZ4 && size > 0)
		{
			if (size > static_cast<size_t>(INT_MAX))
			{
				vfLogWarning("VFPakWriter: File too large for LZ4 ({} bytes), storing uncompressed: {}", size, archivePath);
				compression = CompressionType::None;
			}
		}

		if (compression == CompressionType::LZ4 && size > 0)
		{
			int maxCompressedSize = LZ4_compressBound(static_cast<int>(size));
			std::vector<char> compressed(maxCompressedSize);

			int compressedSize = LZ4_compress_HC(
				static_cast<const char*>(data),
				compressed.data(),
				static_cast<int>(size),
				maxCompressedSize,
				LZ4HC_CLEVEL_DEFAULT);

			if (compressedSize > 0 && static_cast<size_t>(compressedSize) < size)
			{
				// Compression actually reduced size
				entry.compressionType = CompressionType::LZ4;
				entry.compressedSize = static_cast<uint64_t>(compressedSize);
				writeAlignedData(compressed.data(), compressedSize, entry);
			}
			else
			{
				// Incompressible data, store raw
				entry.compressionType = CompressionType::None;
				entry.compressedSize = size;
				writeAlignedData(data, size, entry);
			}
		}
		else
		{
			entry.compressionType = CompressionType::None;
			entry.compressedSize = size;
			writeAlignedData(data, size, entry);
		}

		entries.push_back(std::move(entry));
		return true;
	}

	bool VFPakWriter::writeAlignedData(const void* data, size_t size, VFPakEntry& entry)
	{
		// Align current offset
		size_t aligned = alignTo(currentOffset, VFPAK_ALIGNMENT);
		if (aligned > currentOffset)
		{
			size_t padding = aligned - currentOffset;
			std::vector<char> zeros(padding, 0);
			file.write(zeros.data(), static_cast<std::streamsize>(padding));
			currentOffset = aligned;
		}

		entry.dataOffset = currentOffset;
		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		currentOffset += size;

		return file.good();
	}

	bool VFPakWriter::finalize()
	{
		if (!file.is_open())
		{
			return false;
		}

		// Align before TOC
		size_t aligned = alignTo(currentOffset, VFPAK_ALIGNMENT);
		if (aligned > currentOffset)
		{
			size_t padding = aligned - currentOffset;
			std::vector<char> zeros(padding, 0);
			file.write(zeros.data(), static_cast<std::streamsize>(padding));
			currentOffset = aligned;
		}

		uint64_t tocOffset = currentOffset;

		// Write TOC entries
		for (const auto& entry : entries)
		{
			file.write(reinterpret_cast<const char*>(&entry.pathHash), sizeof(entry.pathHash));
			file.write(reinterpret_cast<const char*>(&entry.dataOffset), sizeof(entry.dataOffset));
			file.write(reinterpret_cast<const char*>(&entry.compressedSize), sizeof(entry.compressedSize));
			file.write(reinterpret_cast<const char*>(&entry.uncompressedSize), sizeof(entry.uncompressedSize));

			auto compType = static_cast<uint8_t>(entry.compressionType);
			file.write(reinterpret_cast<const char*>(&compType), sizeof(compType));
			file.write(reinterpret_cast<const char*>(&entry.flags), sizeof(entry.flags));

			uint16_t pathLen = static_cast<uint16_t>(entry.path.size());
			file.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
			file.write(entry.path.data(), pathLen);
		}

		uint64_t tocSize = static_cast<uint64_t>(file.tellp()) - tocOffset;

		// Patch header
		file.seekp(0);
		VFPakHeader header{};
		header.entryCount = static_cast<uint32_t>(entries.size());
		header.tocOffset = tocOffset;
		header.tocSize = tocSize;
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));

		file.close();

		vfLogInfo("VFPakWriter: Archive created with {} entries at {}",
		          entries.size(), outputPath.string());

		return true;
	}
}
