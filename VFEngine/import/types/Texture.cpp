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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>


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
			hdrData.numbersOfChannels = 4;  // Always store as RGBA

			// Convert to 4 channels (RGBA) for GPU compatibility
			hdrData.textureData = convertToRGBA32F(imageData, width, height, channels);

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

			// LoadEXR always returns RGBA (4 channels)
			int result = LoadEXR(&out, &width, &height, filePath.c_str(), &exrError);
			if (result != TINYEXR_SUCCESS)
			{
				vfLogError("Failed to load EXR image: {}", exrError);
				FreeEXRErrorMessage(exrError);
				FreeEXRImage(&exrImage);
				FreeEXRHeader(&exrHeader);
				return;
			}

			// Report 50% - EXR data extracted
			if (progressCallback) progressCallback(0.5f);

			if (file.config.isImageFlipVertically)
			{
				flipImageVertically(out, width, height);
			}

			hdrData.width = static_cast<uint32_t>(width);
			hdrData.height = static_cast<uint32_t>(height);
			hdrData.numbersOfChannels = 4;  // Always store as RGBA

			// LoadEXR returns RGBA, copy directly
			size_t pixelCount = static_cast<size_t>(width) * height;
			hdrData.textureData = std::vector<float>(out, out + pixelCount * 4);

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

		// Write raw float data (RGBA32F)
		for (float value : hdrData.textureData)
		{
			resource::endian::writeLE<float>(outFile, value);
		}

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

	std::vector<float> Texture::convertToRGBA32F(const float* data, int width, int height, int channels) const
	{
		size_t pixelCount = static_cast<size_t>(width) * height;
		std::vector<float> result(pixelCount * 4);

		for (size_t i = 0; i < pixelCount; ++i)
		{
			float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

			if (channels == 1)
			{
				// Grayscale -> RGBA
				r = g = b = data[i];
			}
			else if (channels == 2)
			{
				// Grayscale + Alpha -> RGBA
				r = g = b = data[i * 2];
				a = data[i * 2 + 1];
			}
			else if (channels == 3)
			{
				// RGB -> RGBA
				r = data[i * 3];
				g = data[i * 3 + 1];
				b = data[i * 3 + 2];
			}
			else if (channels >= 4)
			{
				// Already RGBA
				r = data[i * 4];
				g = data[i * 4 + 1];
				b = data[i * 4 + 2];
				a = data[i * 4 + 3];
			}

			result[i * 4 + 0] = r;
			result[i * 4 + 1] = g;
			result[i * 4 + 2] = b;
			result[i * 4 + 3] = a;
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

	void TGAWriter::writeTGA(std::ofstream& file,const std::vector<unsigned char>& pixelData)
	{

		for (size_t i = 0; i < pixelData.size(); i += 4)
		{
			uint8_t bgra[4] = { pixelData[i + 2], pixelData[i + 1], pixelData[i], pixelData[i + 3] };
			file.write(reinterpret_cast<char*>(bgra), sizeof(bgra));
		}

	}
}
