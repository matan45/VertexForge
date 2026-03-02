#pragma once
#include <filesystem>
#include <string>

namespace gameExport
{
	class ExeIconEmbedder
	{
	public:
		// Embeds an .ico file as the main icon in a Windows .exe
		static bool embedIcon(const std::filesystem::path& exePath,
							  const std::filesystem::path& iconPath,
							  std::string& errorOut);
	};
}
