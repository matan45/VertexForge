#pragma once

#include "../export/GameExportExport.hpp"
#include "VFPakFormat.hpp"
#include <filesystem>
#include <fstream>
#include <functional>

namespace archive
{
	using PackProgressCallback = std::function<void(uint32_t filesProcessed, uint32_t totalFiles,
	                                                 const std::string& currentFile)>;

	#pragma warning(push)
	#pragma warning(disable: 4251)
	class VF_GAMEEXPORT_API VFPakWriter
	{
	public:
		bool create(const std::filesystem::path& outputPath);

		bool addFile(const std::string& archivePath,
		             const std::filesystem::path& sourcePath,
		             CompressionType compression = CompressionType::None);

		bool addMemory(const std::string& archivePath,
		               const void* data, size_t size,
		               CompressionType compression = CompressionType::None);

		bool finalize();

		uint32_t getEntryCount() const { return static_cast<uint32_t>(entries.size()); }

	private:
		std::ofstream file;
		std::filesystem::path outputPath;
		std::vector<VFPakEntry> entries;
		uint64_t currentOffset = VFPAK_HEADER_SIZE;

		bool writeAlignedData(const void* data, size_t size, VFPakEntry& entry);
	};
	#pragma warning(pop)
}
