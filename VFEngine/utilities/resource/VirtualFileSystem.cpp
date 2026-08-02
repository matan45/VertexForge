#include "VirtualFileSystem.hpp"
#include "../archive/VFPakReader.hpp"
#include "../print/Log.hpp"

namespace resource
{
	namespace
	{
		std::string normalizeArchivePath(const std::string& path)
		{
			std::string normalized = path;
			for (char& c : normalized)
			{
				if (c == '\\') c = '/';
			}
			return normalized;
		}
	}

	VirtualFileSystem& VirtualFileSystem::instance()
	{
		static VirtualFileSystem vfs;
		return vfs;
	}

	void VirtualFileSystem::initialize()
	{
		std::unique_lock lock(vfsMutex);
		auto pakPath = findVfpakFile();
		if (pakPath.empty())
		{
			archiveMode = false;
			return;
		}

		archive = std::make_unique<archive::VFPakReader>();
		if (archive->open(pakPath))
		{
			archiveMode = true;
			vfLogInfo("VFS: Archive mode enabled with {} entries", archive->getEntryCount());
		}
		else
		{
			archive.reset();
			archiveMode = false;
			vfLogWarning("VFS: Failed to open archive, falling back to filesystem");
		}
	}

	void VirtualFileSystem::shutdown()
	{
		std::unique_lock lock(vfsMutex);
		if (archive)
		{
			archive->close();
			archive.reset();
		}
		archiveMode = false;
	}

	bool VirtualFileSystem::isArchiveMode() const
	{
		std::shared_lock lock(vfsMutex);
		return archiveMode;
	}

	std::vector<uint8_t> VirtualFileSystem::readFile(const std::string& path) const
	{
		std::shared_lock lock(vfsMutex);
		if (archiveMode && archive)
		{
			const std::string normalized = normalizeArchivePath(path);

			if (archive->contains(normalized))
			{
				return archive->readEntry(normalized);
			}
		}

		// Fallback to filesystem
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			return {};
		}

		auto size = static_cast<size_t>(file.tellg());
		file.seekg(0);

		std::vector<uint8_t> data(size);
		file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));

		return data;
	}

	std::optional<VirtualFileSystem::StreamRegion> VirtualFileSystem::openStream(const std::string& path) const
	{
		std::shared_lock lock(vfsMutex);
		if (archiveMode && archive)
		{
			const std::string normalized = normalizeArchivePath(path);

			auto location = archive->getEntryLocation(normalized);
			if (location)
			{
				StreamRegion region;
				region.stream.open(archive->getArchivePath(), std::ios::binary);
				if (!region.stream.is_open())
				{
					return std::nullopt;
				}
				region.baseOffset = static_cast<std::streampos>(location->offset);
				region.regionSize = location->size;
				region.stream.seekg(region.baseOffset);
				return region;
			}
		}

		// Fallback to filesystem
		StreamRegion region;
		region.stream.open(path, std::ios::binary);
		if (!region.stream.is_open())
		{
			return std::nullopt;
		}

		// Get file size
		region.stream.seekg(0, std::ios::end);
		region.regionSize = static_cast<uint64_t>(region.stream.tellg());
		region.stream.seekg(0);
		region.baseOffset = 0;

		return region;
	}

	std::optional<PhysicalLocation> VirtualFileSystem::locate(const std::string& path) const
	{
		std::shared_lock lock(vfsMutex);
		if (archiveMode && archive)
		{
			const std::string normalized = normalizeArchivePath(path);
			if (archive->contains(normalized))
			{
				auto location = archive->getEntryLocation(normalized);
				if (!location)
				{
					// The logical key exists but is compressed. Never shadow it with
					// a same-named loose file because those bytes are not equivalent.
					return std::nullopt;
				}

				return PhysicalLocation{
					archive->getArchivePath().string(), location->offset, location->size};
			}
		}

		std::error_code ec;
		const uint64_t size = std::filesystem::file_size(path, ec);
		if (ec)
		{
			return std::nullopt;
		}
		return PhysicalLocation{path, 0, size};
	}

	bool VirtualFileSystem::exists(const std::string& path) const
	{
		std::shared_lock lock(vfsMutex);
		if (archiveMode && archive)
		{
			const std::string normalized = normalizeArchivePath(path);

			if (archive->contains(normalized))
			{
				return true;
			}
		}

		return std::filesystem::exists(path);
	}

	std::filesystem::path VirtualFileSystem::findVfpakFile() const
	{
		namespace fs = std::filesystem;
		std::error_code ec;

		// Search for .vfpak in current directory
		for (const auto& entry : fs::directory_iterator(".", ec))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".vfpak")
			{
				return entry.path();
			}
		}

		return {};
	}
}
