#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace archive
{
	constexpr uint32_t VFPAK_MAGIC = 0x4B505656; // "VFPK" little-endian
	constexpr uint32_t VFPAK_VERSION = 1;
	constexpr size_t VFPAK_ALIGNMENT = 16;
	constexpr size_t VFPAK_HEADER_SIZE = 64;

	enum class CompressionType : uint8_t
	{
		None = 0,
		LZ4 = 1
	};

	struct VFPakHeader
	{
		uint32_t magic = VFPAK_MAGIC;
		uint32_t version = VFPAK_VERSION;
		uint32_t flags = 0;
		uint32_t entryCount = 0;
		uint64_t tocOffset = 0;
		uint64_t tocSize = 0;
		uint8_t reserved[32] = {};
	};

	static_assert(sizeof(VFPakHeader) == VFPAK_HEADER_SIZE,
		"VFPakHeader must be exactly 64 bytes");

	struct VFPakEntry
	{
		uint64_t pathHash = 0;
		uint64_t dataOffset = 0;
		uint64_t compressedSize = 0;
		uint64_t uncompressedSize = 0;
		CompressionType compressionType = CompressionType::None;
		uint8_t flags = 0;
		std::string path;
	};

	inline uint64_t hashPath(const std::string& path)
	{
		// FNV-1a 64-bit hash
		uint64_t hash = 14695981039346656037ULL;
		for (char c : path)
		{
			// Normalize separators
			char normalized = (c == '\\') ? '/' : c;
			hash ^= static_cast<uint64_t>(normalized);
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	inline uint64_t hashBytes(const uint8_t* data, size_t size)
	{
		uint64_t hash = 14695981039346656037ULL;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= static_cast<uint64_t>(data[i]);
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	inline size_t alignTo(size_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}
}
