#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <fstream>
#include <filesystem>

namespace archive { class VFPakReader; }

namespace resource
{
	class VirtualFileSystem
	{
	public:
		static VirtualFileSystem& instance();

		void initialize();
		void shutdown();

		bool isArchiveMode() const { return archiveMode; }

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

		// Check if a path exists (filesystem or archive)
		bool exists(const std::string& path) const;

	private:
		VirtualFileSystem() = default;
		~VirtualFileSystem() = default;

		VirtualFileSystem(const VirtualFileSystem&) = delete;
		VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

		std::unique_ptr<archive::VFPakReader> archive;
		bool archiveMode = false;

		std::filesystem::path findVfpakFile() const;
	};
}
