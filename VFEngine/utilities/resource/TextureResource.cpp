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

		// Validate the version
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
		{
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {}; // Return an empty HDRData on version mismatch
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

		// Validate the version
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
		{
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read HDR dimensions (endian-safe)
		hdrData.width = endian::readLE<uint32_t>(inFile);
		hdrData.height = endian::readLE<uint32_t>(inFile);
		hdrData.numbersOfChannels = endian::readLE<uint32_t>(inFile);

		HDRReader::readHDR(inFile, hdrData.width, hdrData.height, hdrData.textureData);

		inFile.close();

		return hdrData;
	}

	void HDRReader::readHDR(std::ifstream& file, int width, int height, std::vector<float>& pixels)
	{
		pixels.resize(width * height * 3);

		// Read pixel data
		for (int y = 0; y < height; ++y)
		{
			// Read and validate scanline header
			uint8_t scanlineHeader[4];
			file.read(reinterpret_cast<char*>(scanlineHeader), 4);
			if (scanlineHeader[0] != 2 || scanlineHeader[1] != 2 ||
				(scanlineHeader[2] << 8 | scanlineHeader[3]) != width)
			{
				vfLogError("Invalid or unsupported scanline header in HDR file.");
				return;
			}

			std::vector<uint8_t> scanline(width * 4);
			for (int channel = 0; channel < 4; ++channel)
			{
				int x = 0;
				while (x < width)
				{
					uint8_t count = file.get();
					if (count > 128)
					{
						// RLE-encoded run
						count -= 128;
						uint8_t value = file.get();
						for (int i = 0; i < count; ++i)
						{
							scanline[x * 4 + channel] = value;
							++x;
						}
					}
					else
					{
						// Raw data
						for (int i = 0; i < count; ++i)
						{
							scanline[x * 4 + channel] = file.get();
							++x;
						}
					}
				}
			}

			// Decode RGBE data
			for (int x = 0; x < width; ++x)
			{
				const RGBE& rgbe = *reinterpret_cast<const RGBE*>(&scanline[x * 4]);
				decodeRGBE(rgbe, pixels[(y * width + x) * 3 + 0],
					pixels[(y * width + x) * 3 + 1],
					pixels[(y * width + x) * 3 + 2]);
			}
		}
	}

	void HDRReader::decodeRGBE(const RGBE& rgbe, float& r, float& g, float& b)
	{
		if (rgbe.e == 0)
		{
			r = g = b = 0.0f;
		}
		else
		{
			float scale = std::ldexp(1.0f, rgbe.e - 128 - 8); // 2^(e - 128) / 256
			r = rgbe.r * scale;
			g = rgbe.g * scale;
			b = rgbe.b * scale;
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
