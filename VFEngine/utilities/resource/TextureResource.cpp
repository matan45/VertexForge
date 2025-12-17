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

		// Validate the version (allow backward compatibility with 0.0.1 for texture format)
		bool versionOk = (majorVersion == Version::major && minorVersion == Version::minor &&
		                  (patchVersion == Version::patch || patchVersion == 1));
		if (!versionOk)
		{
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {}; // Return an empty TextureData on version mismatch
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

		TGAReader::readTGA(inFile, textureData.width, textureData.height, textureData.textureData);
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

		// Validate the version (allow backward compatibility with 0.0.1 for texture format)
		bool versionOk = (majorVersion == Version::major && minorVersion == Version::minor &&
		                  (patchVersion == Version::patch || patchVersion == 1));
		if (!versionOk)
		{
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read HDR dimensions (endian-safe)
		hdrData.width = endian::readLE<uint32_t>(inFile);
		hdrData.height = endian::readLE<uint32_t>(inFile);
		hdrData.numbersOfChannels = endian::readLE<uint32_t>(inFile);

		HDRReader::readHDR(inFile, hdrData.width, hdrData.height, hdrData.numbersOfChannels, hdrData.textureData);

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
