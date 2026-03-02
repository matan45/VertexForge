#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gameExport
{
	class ExeIconEmbedder
	{
	public:
		// Embeds an .ico file as the main icon in a Windows .exe
		static bool embedIcon(const std::filesystem::path& exePath,
							  const std::filesystem::path& iconPath,
							  std::string& errorOut);

	private:
		static bool readIcoFile(const std::filesystem::path& iconPath,
								std::vector<uint8_t>& icoData,
								uint16_t& imageCount,
								std::string& errorOut);

		static bool writeIconResources(const std::filesystem::path& exePath,
									   const std::vector<uint8_t>& icoData,
									   uint16_t imageCount,
									   std::string& errorOut);
	};
}
