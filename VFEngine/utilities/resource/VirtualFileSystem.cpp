#include "VirtualFileSystem.hpp"
#include "ArchivePathKey.hpp"
#include "../archive/VFPakReader.hpp"
#include "../launch/ProjectPathRules.hpp"
#include "../print/Log.hpp"

#include <system_error>

namespace resource
{
	namespace
	{
		namespace fs = std::filesystem;

		// "C:\Game\" -> "C:\Game"; a drive root keeps its separator.
		fs::path withoutTrailingSeparator(const fs::path& folder)
		{
			if (!folder.has_filename() && folder.has_relative_path())
			{
				return folder.parent_path();
			}
			return folder;
		}

		// The folder that holds the .vfpak (found in the working directory), as archiveKeyFor compares it: its
		// canonical form (what fs::relative measures from, junctions and short names resolved), then its
		// absolute spelling when that differs (what fs::absolute builds on, VK-1726). Empty when the folder
		// cannot be made absolute: absolute paths then simply miss the archive, as before.
		std::vector<fs::path> archiveRootsFor(const fs::path& pakPath)
		{
			std::vector<fs::path> roots;
			std::error_code ec;
			const fs::path absoluteFolder = fs::absolute(pakPath.parent_path(), ec);
			if (ec || absoluteFolder.empty())
			{
				return roots;
			}
			const fs::path spelled = withoutTrailingSeparator(absoluteFolder.lexically_normal());

			const fs::path canonical = fs::weakly_canonical(absoluteFolder, ec);
			if (!ec && !canonical.empty())
			{
				roots.push_back(withoutTrailingSeparator(canonical.lexically_normal()));
			}
			if (roots.empty() || !launch::componentEquals(roots.front(), spelled))
			{
				roots.push_back(spelled);
			}
			return roots;
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
			archiveRoots.clear();
			return;
		}

		archive = std::make_unique<archive::VFPakReader>();
		if (archive->open(pakPath))
		{
			archiveMode = true;
			archiveRoots = archiveRootsFor(pakPath);
			vfLogInfo("VFS: Archive mode enabled with {} entries", archive->getEntryCount());
		}
		else
		{
			archive.reset();
			archiveMode = false;
			archiveRoots.clear();
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
		archiveRoots.clear();
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
			const std::string normalized = archiveKeyFor(path, archiveRoots);

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
			const std::string normalized = archiveKeyFor(path, archiveRoots);

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
			const std::string normalized = archiveKeyFor(path, archiveRoots);
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
			const std::string normalized = archiveKeyFor(path, archiveRoots);

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
