#include "ExeIconEmbedder.hpp"
#include "../print/EditorLogger.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <fstream>
#endif

namespace gameExport
{
	bool ExeIconEmbedder::readIcoFile(const std::filesystem::path& iconPath,
									  std::vector<uint8_t>& icoData,
									  uint16_t& imageCount,
									  std::string& errorOut)
	{
#ifdef _WIN32
		std::ifstream icoFile(iconPath, std::ios::binary | std::ios::ate);
		if (!icoFile.is_open())
		{
			errorOut = "Failed to open icon file: " + iconPath.string();
			return false;
		}

		auto fileSize = icoFile.tellg();
		icoFile.seekg(0);
		icoData.resize(static_cast<size_t>(fileSize));
		icoFile.read(reinterpret_cast<char*>(icoData.data()), fileSize);
		icoFile.close();

		if (icoData.size() < 6)
		{
			errorOut = "Invalid .ico file (too small)";
			return false;
		}

		// Validate ICO header
		uint16_t reserved = icoData[0] | (icoData[1] << 8);
		uint16_t type = icoData[2] | (icoData[3] << 8);
		imageCount = icoData[4] | (icoData[5] << 8);

		if (reserved != 0 || type != 1 || imageCount == 0)
		{
			errorOut = "Invalid .ico file format";
			return false;
		}

		return true;
#else
		errorOut = "Icon embedding is only supported on Windows";
		return false;
#endif
	}

	bool ExeIconEmbedder::writeIconResources(const std::filesystem::path& exePath,
											  const std::vector<uint8_t>& icoData,
											  uint16_t imageCount,
											  std::string& errorOut)
	{
#ifdef _WIN32
		HANDLE hUpdate = BeginUpdateResourceW(exePath.wstring().c_str(), FALSE);
		if (!hUpdate)
		{
			errorOut = "Failed to open executable for resource update";
			return false;
		}

		// Build RT_GROUP_ICON header
		// GRPICONDIR: 6 bytes header + imageCount * 14 bytes per entry
		size_t grpSize = 6 + imageCount * 14;
		std::vector<uint8_t> grpIconDir(grpSize);

		// Copy ICONDIR header (reserved, type, count)
		grpIconDir[0] = icoData[0]; grpIconDir[1] = icoData[1];
		grpIconDir[2] = icoData[2]; grpIconDir[3] = icoData[3];
		grpIconDir[4] = icoData[4]; grpIconDir[5] = icoData[5];

		for (uint16_t i = 0; i < imageCount; ++i)
		{
			size_t icoEntryOffset = 6 + i * 16;   // ICO directory entry: 16 bytes each
			size_t grpEntryOffset = 6 + i * 14;    // GRP directory entry: 14 bytes each

			if (icoEntryOffset + 16 > icoData.size())
			{
				EndUpdateResourceW(hUpdate, TRUE);
				errorOut = "Invalid .ico directory entry";
				return false;
			}

			// Copy first 12 bytes of directory entry (width, height, colors, reserved, planes, bitcount, size)
			for (int b = 0; b < 12; ++b)
			{
				grpIconDir[grpEntryOffset + b] = icoData[icoEntryOffset + b];
			}

			// Replace the last 4 bytes (file offset in ICO) with icon ID (1-based)
			uint16_t iconId = i + 1;
			grpIconDir[grpEntryOffset + 12] = static_cast<uint8_t>(iconId & 0xFF);
			grpIconDir[grpEntryOffset + 13] = static_cast<uint8_t>((iconId >> 8) & 0xFF);

			// Get image data offset and size from ICO entry
			uint32_t imageSize = icoData[icoEntryOffset + 8] |
								 (icoData[icoEntryOffset + 9] << 8) |
								 (icoData[icoEntryOffset + 10] << 16) |
								 (icoData[icoEntryOffset + 11] << 24);

			uint32_t imageOffset = icoData[icoEntryOffset + 12] |
								   (icoData[icoEntryOffset + 13] << 8) |
								   (icoData[icoEntryOffset + 14] << 16) |
								   (icoData[icoEntryOffset + 15] << 24);

			if (imageOffset + imageSize > icoData.size())
			{
				EndUpdateResourceW(hUpdate, TRUE);
				errorOut = "Invalid .ico image data";
				return false;
			}

			// Write individual icon image as RT_ICON resource
			if (!UpdateResourceW(hUpdate, RT_ICON, MAKEINTRESOURCEW(iconId),
								 MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
								 const_cast<uint8_t*>(icoData.data()) + imageOffset, imageSize))
			{
				EndUpdateResourceW(hUpdate, TRUE);
				errorOut = "Failed to update RT_ICON resource";
				return false;
			}
		}

		// Write RT_GROUP_ICON resource
		if (!UpdateResourceW(hUpdate, RT_GROUP_ICON, MAKEINTRESOURCEW(1),
							 MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
							 grpIconDir.data(), static_cast<DWORD>(grpIconDir.size())))
		{
			EndUpdateResourceW(hUpdate, TRUE);
			errorOut = "Failed to update RT_GROUP_ICON resource";
			return false;
		}

		if (!EndUpdateResourceW(hUpdate, FALSE))
		{
			errorOut = "Failed to finalize resource update";
			return false;
		}

		return true;
#else
		errorOut = "Icon embedding is only supported on Windows";
		return false;
#endif
	}

	bool ExeIconEmbedder::embedIcon(const std::filesystem::path& exePath,
									const std::filesystem::path& iconPath,
									std::string& errorOut)
	{
#ifdef _WIN32
		if (!std::filesystem::exists(iconPath))
		{
			errorOut = "Icon file not found: " + iconPath.string();
			return false;
		}

		if (!std::filesystem::exists(exePath))
		{
			errorOut = "Executable not found: " + exePath.string();
			return false;
		}

		std::vector<uint8_t> icoData;
		uint16_t imageCount = 0;
		if (!readIcoFile(iconPath, icoData, imageCount, errorOut))
			return false;

		if (!writeIconResources(exePath, icoData, imageCount, errorOut))
			return false;

		vfLogInfo("Icon embedded successfully into {}", exePath.string());
		return true;
#else
		errorOut = "Icon embedding is only supported on Windows";
		return false;
#endif
	}
}
