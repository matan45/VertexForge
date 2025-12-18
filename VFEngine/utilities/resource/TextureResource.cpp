#include "TextureResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast

namespace resource
{
	TextureData TextureResource::loadTexture(std::string_view path)
	{
		resource::TextureData textureData;

		// Validate input
		if (path.empty()) {
			vfLogError("Empty path provided for texture loading");
			return {};
		}

		// Open the file in binary mode
		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile)
		{
			vfLogError("Failed to open texture file for reading: {}", path);
			return {};
		}

		// Read header file type (single byte, endian-safe)
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		textureData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version information (endian-safe)
		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		// Determine format version
		bool isMipFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
		bool isLegacyFormat = (majorVersion == 0 && minorVersion == 0 && (patchVersion == 1 || patchVersion == 2));

		if (!isMipFormat && !isLegacyFormat)
		{
			vfLogError("Incompatible texture file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read texture dimensions (endian-safe)
		textureData.width = endian::readLE<uint32_t>(inFile);
		textureData.height = endian::readLE<uint32_t>(inFile);
		textureData.numbersOfChannels = endian::readLE<uint32_t>(inFile);

		// Validate texture dimensions
		if (textureData.width == 0 || textureData.height == 0) {
			vfLogError("Invalid texture dimensions: {}x{}", textureData.width, textureData.height);
			return {};
		}

		if (textureData.width > 16384 || textureData.height > 16384) {
			vfLogError("Texture dimensions {}x{} exceed maximum limit (16384x16384)", textureData.width, textureData.height);
			return {};
		}

		if (textureData.numbersOfChannels == 0 || textureData.numbersOfChannels > 4) {
			vfLogError("Invalid number of channels: {}", textureData.numbersOfChannels);
			return {};
		}

		if (isMipFormat)
		{
			// v0.0.3+ format with mipmaps
			textureData.mipLevels = endian::readLE<uint32_t>(inFile);

			if (textureData.mipLevels == 0 || textureData.mipLevels > 16) {
				vfLogError("Invalid mip level count: {}", textureData.mipLevels);
				return {};
			}

			textureData.mipData.reserve(textureData.mipLevels);

			for (uint32_t level = 0; level < textureData.mipLevels; ++level)
			{
				MipLevelData mipLevel;
				mipLevel.width = endian::readLE<uint32_t>(inFile);
				mipLevel.height = endian::readLE<uint32_t>(inFile);

				// Read pixel data for this mip level
				TGAReader::readTGA(inFile, mipLevel.width, mipLevel.height, mipLevel.data);
				textureData.mipData.push_back(std::move(mipLevel));
			}
		}
		else
		{
			// Legacy format (v0.0.1 or v0.0.2) - single mip level
			textureData.mipLevels = 1;

			MipLevelData mipLevel;
			mipLevel.width = textureData.width;
			mipLevel.height = textureData.height;
			TGAReader::readTGA(inFile, mipLevel.width, mipLevel.height, mipLevel.data);
			textureData.mipData.push_back(std::move(mipLevel));
		}

		inFile.close();

		return textureData;
	}

	HDRData TextureResource::loadHDR(std::string_view path)
	{
		resource::HDRData hdrData;

		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile)
		{
			vfLogError("Failed to open file for reading: ", path);
			return {};
		}

		// Read the header file type (endian-safe)
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		hdrData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version (endian-safe)
		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		// Determine format version
		bool isMipFormat = (majorVersion == 0 && minorVersion == 0 && patchVersion >= 3);
		bool isLegacyFormat = (majorVersion == 0 && minorVersion == 0 && (patchVersion == 1 || patchVersion == 2));

		if (!isMipFormat && !isLegacyFormat)
		{
			vfLogError("Incompatible HDR file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read HDR dimensions (endian-safe)
		hdrData.width = endian::readLE<uint32_t>(inFile);
		hdrData.height = endian::readLE<uint32_t>(inFile);
		hdrData.numbersOfChannels = endian::readLE<uint32_t>(inFile);

		if (isMipFormat)
		{
			// v0.0.3+ format with mipmaps
			hdrData.mipLevels = endian::readLE<uint32_t>(inFile);

			if (hdrData.mipLevels == 0 || hdrData.mipLevels > 16) {
				vfLogError("Invalid HDR mip level count: {}", hdrData.mipLevels);
				return {};
			}

			hdrData.mipData.reserve(hdrData.mipLevels);

			for (uint32_t level = 0; level < hdrData.mipLevels; ++level)
			{
				MipLevelDataHDR mipLevel;
				mipLevel.width = endian::readLE<uint32_t>(inFile);
				mipLevel.height = endian::readLE<uint32_t>(inFile);

				// Read float pixel data for this mip level
				HDRReader::readHDR(inFile, mipLevel.width, mipLevel.height, hdrData.numbersOfChannels, mipLevel.data);
				hdrData.mipData.push_back(std::move(mipLevel));
			}
		}
		else
		{
			// Legacy format (v0.0.1 or v0.0.2) - single mip level
			hdrData.mipLevels = 1;

			MipLevelDataHDR mipLevel;
			mipLevel.width = hdrData.width;
			mipLevel.height = hdrData.height;
			HDRReader::readHDR(inFile, mipLevel.width, mipLevel.height, hdrData.numbersOfChannels, mipLevel.data);
			hdrData.mipData.push_back(std::move(mipLevel));
		}

		inFile.close();

		return hdrData;
	}

	void HDRReader::readHDR(std::ifstream& file, int width, int height, int channels, std::vector<float>& pixels)
	{
		size_t pixelCount = static_cast<size_t>(width) * height * channels;
		pixels.resize(pixelCount);

		// Read raw float data
		for (size_t i = 0; i < pixelCount; ++i)
		{
			pixels[i] = endian::readLE<float>(file);
		}
	}

	void TGAReader::readTGA(std::ifstream& file, int width, int height,
		std::vector<unsigned char>& pixelData)
	{
		// Allocate memory for the pixel data
		size_t pixelDataSize = width * height * 4;
		pixelData.resize(pixelDataSize);

		// Read pixel data
		file.read(reinterpret_cast<char*>(pixelData.data()), pixelDataSize);

		for (size_t i = 0; i < pixelDataSize; i += 4)
		{
			std::swap(pixelData[i], pixelData[i + 2]); // Swap B and R
		}

	}
}
