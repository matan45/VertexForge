#include "Texture.hpp"
#include "print/EditorLogger.hpp"
#include "../controllers/files/FileUtils.hpp"
#include "config/Config.hpp"
#include "resource/EndianUtils.hpp"

#include <iostream>
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


#include <vector>
#include <fstream>
#include <bit>
#include <filesystem>


namespace types
{
	void Texture::loadTextureFile(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback)
	{
		// Report 0% - starting load
		if (progressCallback) progressCallback(0.0f);

		resource::TextureData textureData;
		textureData.headerFileType = resource::FileType::TEXTURE;

		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(true);
		}
		// Load image using stb_image
		int width;
		int height;
		int channels;

		std::string filePath(file.path);
		unsigned char* imageData = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

		if (!imageData)
		{
			vfLogError("Failed to load texture: {}", file.path.data());
			return; // Return
		}

		// Report 30% - image loaded from disk
		if (progressCallback) progressCallback(0.3f);

		// Store texture information
		textureData.width = static_cast<uint32_t>(width);
		textureData.height = static_cast<uint32_t>(height);
		textureData.numbersOfChannels = channels;

		convertTo4Channels(imageData, width, height, channels, textureData.textureData);

		// Report 60% - channel conversion complete
		if (progressCallback) progressCallback(0.6f);

		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(false);
		}

		stbi_image_free(imageData);

		saveToFileTexture(fileName, location, textureData);

		// Report 100% - complete
		if (progressCallback) progressCallback(1.0f);
	}

	void Texture::loadHDRFile(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback) const
	{
		// Report 0% - starting load
		if (progressCallback) progressCallback(0.0f);

		resource::HDRData hdrData;
		hdrData.headerFileType = resource::FileType::HDR;

		// File type detection is now handled by the pipeline, determine from extension
		std::string filePath(file.path);
		std::string extension = files::FileUtils::getFileExtension(filePath);
		if (extension == ".hdr")
		{
			if (file.config.isImageFlipVertically)
			{
				stbi_set_flip_vertically_on_load(true);
			}
			// Load image using stb_image
			int width;
			int height;
			int channels;
			float* imageData = stbi_loadf(filePath.c_str(), &width, &height, &channels, 0);
			if (!imageData)
			{
				vfLogError("Failed to load texture: {}", file.path.data());
				return;
			}

			// Report 40% - HDR loaded
			if (progressCallback) progressCallback(0.4f);

			if (file.config.isImageFlipVertically)
			{
				stbi_set_flip_vertically_on_load(false);
			}

			hdrData.width = static_cast<uint32_t>(width);
			hdrData.height = static_cast<uint32_t>(height);
			hdrData.numbersOfChannels = channels;
			hdrData.textureData = std::vector<float>(imageData, imageData + (static_cast<ptrdiff_t>(width) * height * channels));

			// Report 70% - data copied, saving to file
			if (progressCallback) progressCallback(0.7f);

			saveToFileHDR(fileName, location, hdrData);

			stbi_image_free(imageData);

			// Report 100% - complete
			if (progressCallback) progressCallback(1.0f);
		}
		else if (extension == ".exr")
		{
			EXRVersion exrVersion;

			int ret = ParseEXRVersionFromFile(&exrVersion, filePath.c_str());
			if (ret != TINYEXR_SUCCESS)
			{
				vfLogError("Invalid EXR file: {}", filePath);
				return;
			}

			// Report 10% - EXR version parsed
			if (progressCallback) progressCallback(0.1f);

			EXRHeader exrHeader;
			InitEXRHeader(&exrHeader);

			const char* exrError = nullptr;
			ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filePath.c_str(), &exrError);
			if (ret != TINYEXR_SUCCESS)
			{
				vfLogError("Parse EXR err: {}", exrError);
				FreeEXRErrorMessage(exrError);
				return;
			}

			// Report 20% - EXR header parsed
			if (progressCallback) progressCallback(0.2f);

			EXRImage exrImage;
			InitEXRImage(&exrImage);

			ret = LoadEXRImageFromFile(&exrImage, &exrHeader, filePath.c_str(), &exrError);
			if (ret != TINYEXR_SUCCESS)
			{
				vfLogError("Load EXR err: {}", exrError);
				FreeEXRHeader(&exrHeader);
				FreeEXRErrorMessage(exrError);
				return;
			}

			// Report 40% - EXR image loaded
			if (progressCallback) progressCallback(0.4f);

			float* out;
			int width;
			int height;

			int result = LoadEXR(&out, &width, &height, filePath.c_str(), &exrError);
			if (result != TINYEXR_SUCCESS)
			{
				vfLogError("Failed to load EXR image: {}", exrError);
				FreeEXRErrorMessage(exrError);
				return;
			}

			// Report 50% - EXR data extracted
			if (progressCallback) progressCallback(0.5f);

			if (file.config.isImageFlipVertically)
			{
				flipImageVertically(out, width, height);
			}

			int channels = exrImage.num_channels < 4 ? exrImage.num_channels : 4;

			hdrData.width = static_cast<uint32_t>(width);
			hdrData.height = static_cast<uint32_t>(height);
			hdrData.numbersOfChannels = static_cast<uint32_t>(channels);
			hdrData.textureData = convertFromEXRToHDR(out, width, height);

			// Report 70% - conversion complete
			if (progressCallback) progressCallback(0.7f);

			free(out);
			FreeEXRImage(&exrImage);
			FreeEXRHeader(&exrHeader);

			saveToFileHDR(fileName, location, hdrData);

			// Report 100% - complete
			if (progressCallback) progressCallback(1.0f);
		}
		else {
			vfLogError("Unsupported HDR file extension: {}", extension);
		}
	}
	

	void Texture::saveToFileTexture(std::string_view fileName, std::string_view location,
		const resource::TextureData& textureData) const
	{
		// Open the file in binary mode
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
			FileExtension::textrue);
		std::ofstream outFile(newFileLocation, std::ios::binary);

		if (!outFile)
		{
			vfLogError("Failed to open file for writing: ", newFileLocation.string());
			return;
		}

		// Write header, version, and dimensions (endian-safe)
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(textureData.headerFileType));
		resource::endian::writeLE<uint32_t>(outFile, Version::major);
		resource::endian::writeLE<uint32_t>(outFile, Version::minor);
		resource::endian::writeLE<uint32_t>(outFile, Version::patch);
		resource::endian::writeLE<uint32_t>(outFile, textureData.width);
		resource::endian::writeLE<uint32_t>(outFile, textureData.height);
		resource::endian::writeLE<uint32_t>(outFile, textureData.numbersOfChannels);

		TGAWriter::writeTGA(outFile, textureData.textureData);

		// Close the file
		outFile.close();
	}

	void Texture::saveToFileHDR(std::string_view fileName, std::string_view location,
		const resource::HDRData& hdrData) const
	{
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
			FileExtension::hdr);
		std::ofstream outFile(newFileLocation, std::ios::binary);

		if (!outFile)
		{
			vfLogError("Failed to open file for writing: ", newFileLocation.string());
			return;
		}

		// Write HDR header, version, and dimensions (endian-safe)
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(hdrData.headerFileType));
		resource::endian::writeLE<uint32_t>(outFile, Version::major);
		resource::endian::writeLE<uint32_t>(outFile, Version::minor);
		resource::endian::writeLE<uint32_t>(outFile, Version::patch);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.width);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.height);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.numbersOfChannels);

		HDRWriter::writeHDR(outFile, static_cast<int>(hdrData.width), static_cast<int>(hdrData.height),
		                    static_cast<int>(hdrData.numbersOfChannels), hdrData.textureData);

		outFile.close();
	}

	void Texture::convertTo4Channels(unsigned char* inputData, int width, int height, int inputChannels, std::vector<unsigned char>& outputData)
	{
		int outputChannels = 4; // RGBA
		size_t totalPixels = static_cast<size_t>(width) * height;
		outputData.resize(totalPixels * outputChannels);

		for (size_t i = 0; i < totalPixels; ++i) {
			unsigned char r = 0, g = 0, b = 0, a = 255;
			if (inputChannels == 1) {
				// Grayscale -> RGBA
				r = g = b = inputData[i];
				a = 255; // Default alpha
			}
			else if (inputChannels == 2) {
				// Grayscale + Alpha -> RGBA
				r = g = b = inputData[i * 2];
				a = inputData[i * 2 + 1];
			}
			else if (inputChannels == 3) {
				// RGB -> RGBA
				r = inputData[i * 3];
				g = inputData[i * 3 + 1];
				b = inputData[i * 3 + 2];
				a = 255; // Default alpha
			}
			else if (inputChannels == 4) {
				// Already RGBA
				r = inputData[i * 4];
				g = inputData[i * 4 + 1];
				b = inputData[i * 4 + 2];
				a = inputData[i * 4 + 3];
			}

			outputData[i * 4] = r;
			outputData[i * 4 + 1] = g;
			outputData[i * 4 + 2] = b;
			outputData[i * 4 + 3] = a;
		}
	}

	std::vector<float> Texture::convertFromEXRToHDR(const float* data, int width, int height) const
	{
		std::vector<float> result(static_cast<size_t>(width) * height * 3); // RGB needs 3 floats per pixel
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const int index = (y * width + x) * 4; // EXR data has 4 channels (RGBA)
				float r = data[index];
				float g = data[index + 1];
				float b = data[index + 2];

				// Normalize RGB values to [0, 1]
				float maxValue = std::max({ r, g, b, 1e-6f }); // Avoid division by zero
				if (maxValue > 1.0f)
				{
					r /= maxValue;
					g /= maxValue;
					b /= maxValue;
				}

				// Store normalized values in result
				int resultIndex = (y * width + x) * 3;
				result[resultIndex] = r;
				result[resultIndex + 1] = g;
				result[resultIndex + 2] = b;
			}
		}
		return result;
	}

	void Texture::flipImageVertically(float* imageData, int width, int height) const
	{
		if (!imageData || width <= 0 || height <= 0)
		{
			return; // Invalid input
		}

		size_t rowSize = static_cast<size_t>(width) * 4; // Number of floats per row (RGBA)
		float* tempRow = new float[rowSize]; // Temporary buffer to hold a row

		for (int y = 0; y < height / 2; ++y)
		{
			// Calculate row indices to swap
			float* topRow = imageData + static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(rowSize);
			float* bottomRow = imageData + static_cast<ptrdiff_t>(height - 1 - y) * static_cast<ptrdiff_t>(rowSize);

			// Swap rows
			std::memcpy(tempRow, topRow, rowSize * sizeof(float));
			std::memcpy(topRow, bottomRow, rowSize * sizeof(float));
			std::memcpy(bottomRow, tempRow, rowSize * sizeof(float));
		}

		delete[] tempRow;
	}

	void HDRWriter::writeHDR(std::ofstream& file, int width, int height, int numbersOfChannels,
		const std::vector<float>& pixels)
	{
		if (pixels.size() != static_cast<size_t>(width) * height * numbersOfChannels)
		{
			vfLogError("Pixel data size does not match image dimensions!");
			return;
		}
		for (int y = 0; y < height; ++y)
		{
			// Write scanline header
			uint8_t scanlineHeader[4] = { 2, 2, (uint8_t)(width >> 8), (uint8_t)(width & 0xFF) };
			file.write(reinterpret_cast<char*>(scanlineHeader), 4);

			for (int channel = 0; channel < 4; ++channel)
			{
				int x = 0;
				while (x < width)
				{
					int runLength = 1;
					while (x + runLength < width && runLength < 127 &&
						getChannel(pixels, width, y, x, channel) ==
						getChannel(pixels, width, y, x + runLength, channel))
					{
						runLength++;
					}

					if (runLength > 1)
					{
						// RLE
						uint8_t value = getChannel(pixels, width, y, x, channel);
						file.put(static_cast<char>(128 + runLength));
						file.put(static_cast<char>(value));
					}
					else
					{
						// Raw data
						uint8_t value = getChannel(pixels, width, y, x, channel);
						file.put(static_cast<char>(1));
						file.put(static_cast<char>(value));
					}
					x += runLength;
				}
			}
		}
	}

	uint8_t HDRWriter::getChannel(const std::vector<float>& pixels, int width, int y, int x, int channel)
	{
		float r = pixels[(y * width + x) * 3 + 0];
		float g = pixels[(y * width + x) * 3 + 1];
		float b = pixels[(y * width + x) * 3 + 2];

		switch (channel)
		{
		case 0: return encodeRGBE(r, g, b).r; // Red
		case 1: return encodeRGBE(r, g, b).g; // Green
		case 2: return encodeRGBE(r, g, b).b; // Blue
		case 3: return encodeRGBE(r, g, b).e; // Exponent
		default: return 0;
		}
	}

	RGBE HDRWriter::encodeRGBE(float r, float g, float b)
	{
		float maxColor = std::max(r, std::max(g, b));
		if (maxColor < 1e-5f) return { 0, 0, 0, 0 };

		int e;
		float scale = std::frexp(maxColor, &e) * 256.0f / maxColor;

		return {
			(uint8_t)(r * scale),
			(uint8_t)(g * scale),
			(uint8_t)(b * scale),
			(uint8_t)(e + 128)
		};
	}

	void TGAWriter::writeTGA(std::ofstream& file,const std::vector<unsigned char>& pixelData)
	{

		for (size_t i = 0; i < pixelData.size(); i += 4)
		{
			uint8_t bgra[4] = { pixelData[i + 2], pixelData[i + 1], pixelData[i], pixelData[i + 3] };
			file.write(reinterpret_cast<char*>(bgra), sizeof(bgra));
		}

	}
}
