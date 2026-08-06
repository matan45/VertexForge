#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <fstream>
#include <filesystem>
#include <shared_mutex>

namespace archive { class VFPakReader; }

namespace resource
{
	struct PhysicalLocation
	{
		std::string filePath;
		uint64_t baseOffset = 0;
		uint64_t size = 0;
	};

	class VirtualFileSystem
	{
	public:
		static VirtualFileSystem& instance();

		void initialize();
		void shutdown();

		bool isArchiveMode() const;

		// Read entire file (decompresses LZ4 if needed)
		// In dev mode: reads from filesystem
		// In archive mode: reads from .vfpak
		std::vector<uint8_t> readFile(const std::string& path) const;

		// Streaming access for uncompressed entries
		struct StreamRegion
		{
			std::ifstream stream;
			std::streampos baseOffset;
			uint64_t regionSize;
		};
		std::optional<StreamRegion> openStream(const std::string& path) const;

		// Resolve a logical path to seekable physical bytes. Compressed archive
		// entries deliberately have no physical location.
		std::optional<PhysicalLocation> locate(const std::string& path) const;

		// Check if a path exists (filesystem or archive)
		bool exists(const std::string& path) const;

	private:
		VirtualFileSystem() = default;
		~VirtualFileSystem() = default;

		VirtualFileSystem(const VirtualFileSystem&) = delete;
		VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

		std::unique_ptr<archive::VFPakReader> archive;
		bool archiveMode = false;
		mutable std::shared_mutex vfsMutex;

		std::filesystem::path findVfpakFile() const;
	};
}
