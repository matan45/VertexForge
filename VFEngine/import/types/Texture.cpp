#include "print/Log.hpp"
#include "Texture.hpp"
#include "TextureCompressor.hpp"
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
#include <algorithm>
#include <cstring>


namespace types
{
	void Texture::loadTextureFile(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback)
	{
		if (progressCallback) progressCallback(0.0f);

		resource::TextureData textureData;
		textureData.headerFileType = resource::FileType::TEXTURE;

		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(true);
		}

		int width;
		int height;
		int channels;

		std::string filePath(file.path);
		unsigned char* imageData = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

		if (!imageData)
		{
			vfLogError("Failed to load texture: {}", file.path.data());
			return;
		}

		if (progressCallback) progressCallback(0.15f);

		textureData.width = static_cast<uint32_t>(width);
		textureData.height = static_cast<uint32_t>(height);
		textureData.numbersOfChannels = channels;

		std::vector<unsigned char> rgbaData;
		convertTo4Channels(imageData, width, height, channels, rgbaData);

		textureData.mipData.push_back({
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height),
			0,
			std::move(rgbaData)
		});

		if (progressCallback) progressCallback(0.3f);

		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(false);
		}

		stbi_image_free(imageData);

		generateMipmaps(textureData);

		if (progressCallback) progressCallback(0.5f);

		// Compress mips
		compressTextureMips(textureData, file.config.compressionMode, file.config.compressionQuality);

		if (progressCallback) progressCallback(0.85f);

		saveToFileTextureWithMips(fileName, location, textureData);

		if (progressCallback) progressCallback(1.0f);
	}

	void Texture::loadHDRFile(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback) const
	{
		if (progressCallback) progressCallback(0.0f);

		std::string filePath(file.path);
		std::string extension = files::FileUtils::getFileExtension(filePath);

		if (extension == ".hdr")
		{
			loadHDRFromStbi(file, fileName, location, progressCallback);
		}
		else if (extension == ".exr")
		{
			loadHDRFromEXR(file, fileName, location, progressCallback);
		}
		else
		{
			vfLogError("Unsupported HDR file extension: {}", extension);
		}
	}

	void Texture::loadHDRFromStbi(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback) const
	{
		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(true);
		}

		int width;
		int height;
		int channels;
		std::string filePath(file.path);
		float* imageData = stbi_loadf(filePath.c_str(), &width, &height, &channels, 0);
		if (!imageData)
		{
			vfLogError("Failed to load texture: {}", file.path.data());
			return;
		}

		if (progressCallback) progressCallback(0.2f);

		if (file.config.isImageFlipVertically)
		{
			stbi_set_flip_vertically_on_load(false);
		}

		resource::HDRData hdrData;
		hdrData.headerFileType = resource::FileType::HDR;
		hdrData.width = static_cast<uint32_t>(width);
		hdrData.height = static_cast<uint32_t>(height);
		hdrData.numbersOfChannels = 4;

		auto pixels = convertToRGBA32F(imageData, width, height, channels);
		stbi_image_free(imageData);

		if (progressCallback) progressCallback(0.35f);

		// Generate HDR mipmaps
		generateHDRMipmaps(pixels, hdrData.width, hdrData.height, hdrData);

		if (progressCallback) progressCallback(0.5f);

		// Compress HDR mips
		compressHDRMips(hdrData, file.config.compressionMode, file.config.compressionQuality);

		if (progressCallback) progressCallback(0.85f);

		saveToFileHDRWithMips(fileName, location, hdrData);

		if (progressCallback) progressCallback(1.0f);
	}

	void Texture::loadHDRFromEXR(const importConfig::ImportFiles& file, std::string_view fileName,
		std::string_view location, TextureProgressCallback progressCallback) const
	{
		std::string filePath(file.path);
		EXRVersion exrVersion;

		int ret = ParseEXRVersionFromFile(&exrVersion, filePath.c_str());
		if (ret != TINYEXR_SUCCESS)
		{
			vfLogError("Invalid EXR file: {}", filePath);
			return;
		}

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

		if (progressCallback) progressCallback(0.15f);

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

		if (progressCallback) progressCallback(0.2f);

		float* out;
		int width;
		int height;

		int result = LoadEXR(&out, &width, &height, filePath.c_str(), &exrError);
		if (result != TINYEXR_SUCCESS)
		{
			vfLogError("Failed to load EXR image: {}", exrError);
			FreeEXRErrorMessage(exrError);
			FreeEXRImage(&exrImage);
			FreeEXRHeader(&exrHeader);
			return;
		}

		if (progressCallback) progressCallback(0.3f);

		if (file.config.isImageFlipVertically)
		{
			flipImageVertically(out, width, height);
		}

		resource::HDRData hdrData;
		hdrData.headerFileType = resource::FileType::HDR;
		hdrData.width = static_cast<uint32_t>(width);
		hdrData.height = static_cast<uint32_t>(height);
		hdrData.numbersOfChannels = 4;

		size_t pixelCount = static_cast<size_t>(width) * height * 4;
		std::vector<float> pixels(pixelCount);
		std::memcpy(pixels.data(), out, pixelCount * sizeof(float));

		free(out);
		FreeEXRImage(&exrImage);
		FreeEXRHeader(&exrHeader);

		if (progressCallback) progressCallback(0.4f);

		// Generate HDR mipmaps
		generateHDRMipmaps(pixels, hdrData.width, hdrData.height, hdrData);

		if (progressCallback) progressCallback(0.55f);

		// Compress HDR mips
		compressHDRMips(hdrData, file.config.compressionMode, file.config.compressionQuality);

		if (progressCallback) progressCallback(0.85f);

		saveToFileHDRWithMips(fileName, location, hdrData);

		if (progressCallback) progressCallback(1.0f);
	}


	void Texture::saveToFileTextureWithMips(std::string_view fileName, std::string_view location,
		const resource::TextureData& textureData) const
	{
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
			FileExtension::textrue);
		std::ofstream outFile(newFileLocation, std::ios::binary);

		if (!outFile)
		{
			vfLogError("Failed to open file for writing: ", newFileLocation.string());
			return;
		}

		// Write header
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(textureData.headerFileType));
		resource::endian::writeLE<uint32_t>(outFile, Version::major);
		resource::endian::writeLE<uint32_t>(outFile, Version::minor);
		resource::endian::writeLE<uint32_t>(outFile, Version::patch);
		resource::endian::writeLE<uint32_t>(outFile, textureData.width);
		resource::endian::writeLE<uint32_t>(outFile, textureData.height);
		resource::endian::writeLE<uint32_t>(outFile, textureData.numbersOfChannels);
		resource::endian::writeLE<uint32_t>(outFile, textureData.mipLevels);
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(textureData.compressionFormat));

		// Write each mip level
		for (const auto& mip : textureData.mipData)
		{
			resource::endian::writeLE<uint32_t>(outFile, mip.width);
			resource::endian::writeLE<uint32_t>(outFile, mip.height);
			resource::endian::writeLE<uint32_t>(outFile, mip.dataSize);

			if (textureData.compressionFormat != resource::TextureCompressionFormat::Uncompressed)
			{
				// Write compressed data directly (no BGRA swap)
				outFile.write(reinterpret_cast<const char*>(mip.data.data()), mip.dataSize);
			}
			else
			{
				// Write pixel data in BGRA format (TGA-style)
				TGAWriter::writeTGA(outFile, mip.data);
			}
		}

		outFile.close();
	}

	void Texture::saveToFileHDRWithMips(std::string_view fileName, std::string_view location,
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

		// Write HDR header
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(hdrData.headerFileType));
		resource::endian::writeLE<uint32_t>(outFile, Version::major);
		resource::endian::writeLE<uint32_t>(outFile, Version::minor);
		resource::endian::writeLE<uint32_t>(outFile, Version::patch);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.width);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.height);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.numbersOfChannels);
		resource::endian::writeLE<uint32_t>(outFile, hdrData.mipLevels);
		resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(hdrData.compressionFormat));

		// Write each mip level
		for (const auto& mip : hdrData.mipData)
		{
			resource::endian::writeLE<uint32_t>(outFile, mip.width);
			resource::endian::writeLE<uint32_t>(outFile, mip.height);
			resource::endian::writeLE<uint32_t>(outFile, mip.dataSize);

			// Write data directly (compressed or raw bytes)
			outFile.write(reinterpret_cast<const char*>(mip.data.data()), mip.dataSize);
		}

		outFile.close();
	}

	void Texture::generateMipmaps(resource::TextureData& textureData) const
	{
		if (textureData.mipData.empty())
		{
			vfLogError("Cannot generate mipmaps: no base level data");
			return;
		}

		// Calculate mip levels based on minimum dimension of 512
		// Textures <= 512 get no additional mips
		constexpr uint32_t minMipDimension = 512;
		uint32_t minDimension = std::min(textureData.width, textureData.height);

		if (minDimension <= minMipDimension)
		{
			// Texture is already small, no mips needed
			textureData.mipLevels = 1;
			return;
		}

		// Count how many times we can halve before reaching 512
		textureData.mipLevels = 1;
		uint32_t dim = minDimension;
		while (dim > minMipDimension)
		{
			dim /= 2;
			textureData.mipLevels++;
		}

		// Reserve space for all mip levels
		textureData.mipData.reserve(textureData.mipLevels);

		// Generate each subsequent mip level from the previous one
		for (uint32_t level = 1; level < textureData.mipLevels; ++level)
		{
			const auto& sourceMip = textureData.mipData[level - 1];
			auto newMip = generateMipLevel(sourceMip);
			textureData.mipData.push_back(std::move(newMip));
		}
	}

	void Texture::generateHDRMipmaps(std::vector<float>& basePixels, uint32_t width, uint32_t height,
		resource::HDRData& hdrData) const
	{
		// Store base level as MipLevelData (raw float bytes)
		{
			resource::MipLevelData baseMip;
			baseMip.width = width;
			baseMip.height = height;
			size_t byteSize = static_cast<size_t>(width) * height * 4 * sizeof(float);
			baseMip.dataSize = static_cast<uint32_t>(byteSize);
			baseMip.data.resize(byteSize);
			std::memcpy(baseMip.data.data(), basePixels.data(), byteSize);
			hdrData.mipData.push_back(std::move(baseMip));
		}

		// Calculate mip levels (same policy as LDR)
		constexpr uint32_t minMipDimension = 512;
		uint32_t minDimension = std::min(width, height);

		if (minDimension <= minMipDimension)
		{
			hdrData.mipLevels = 1;
			return;
		}

		hdrData.mipLevels = 1;
		uint32_t dim = minDimension;
		while (dim > minMipDimension)
		{
			dim /= 2;
			hdrData.mipLevels++;
		}

		// Generate HDR mipmaps using float-precision box filter
		for (uint32_t level = 1; level < hdrData.mipLevels; ++level)
		{
			const auto& srcMip = hdrData.mipData[level - 1];
			uint32_t srcW = srcMip.width;
			uint32_t srcH = srcMip.height;
			const float* srcPixels = reinterpret_cast<const float*>(srcMip.data.data());

			uint32_t dstW = std::max(1u, srcW / 2);
			uint32_t dstH = std::max(1u, srcH / 2);

			resource::MipLevelData dstMip;
			dstMip.width = dstW;
			dstMip.height = dstH;
			size_t byteSize = static_cast<size_t>(dstW) * dstH * 4 * sizeof(float);
			dstMip.dataSize = static_cast<uint32_t>(byteSize);
			dstMip.data.resize(byteSize);
			float* dstPixels = reinterpret_cast<float*>(dstMip.data.data());

			// 2x2 box filter in float precision
			for (uint32_t y = 0; y < dstH; ++y)
			{
				for (uint32_t x = 0; x < dstW; ++x)
				{
					uint32_t sx0 = std::min(x * 2, srcW - 1);
					uint32_t sy0 = std::min(y * 2, srcH - 1);
					uint32_t sx1 = std::min(x * 2 + 1, srcW - 1);
					uint32_t sy1 = std::min(y * 2 + 1, srcH - 1);

					size_t i00 = (static_cast<size_t>(sy0) * srcW + sx0) * 4;
					size_t i10 = (static_cast<size_t>(sy0) * srcW + sx1) * 4;
					size_t i01 = (static_cast<size_t>(sy1) * srcW + sx0) * 4;
					size_t i11 = (static_cast<size_t>(sy1) * srcW + sx1) * 4;

					size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * 4;
					for (int c = 0; c < 4; ++c)
					{
						dstPixels[dstIdx + c] = (srcPixels[i00 + c] + srcPixels[i10 + c] +
							srcPixels[i01 + c] + srcPixels[i11 + c]) * 0.25f;
					}
				}
			}

			hdrData.mipData.push_back(std::move(dstMip));
		}
	}

	void Texture::compressTextureMips(resource::TextureData& textureData,
		importConfig::TextureCompressionMode mode,
		importConfig::TextureCompressionQuality quality) const
	{
		if (mode == importConfig::TextureCompressionMode::Uncompressed)
		{
			textureData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;
			return;
		}

		textureData.compressionFormat = resource::TextureCompressionFormat::BC7;

		for (auto& mip : textureData.mipData)
		{
			auto compressed = TextureCompressor::compressBC7(
				mip.data.data(), mip.width, mip.height, quality);

			if (!compressed.empty())
			{
				mip.dataSize = static_cast<uint32_t>(compressed.size());
				mip.data = std::move(compressed);
			}
			else
			{
				vfLogError("Texture compression failed for mip {}x{}", mip.width, mip.height);
				textureData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;
				return;
			}
		}

		vfLogDebug("Compressed texture {}x{} to BC7 ({} mips)",
			textureData.width, textureData.height,
			textureData.mipLevels);
	}

	void Texture::compressHDRMips(resource::HDRData& hdrData,
		importConfig::TextureCompressionMode mode,
		importConfig::TextureCompressionQuality quality) const
	{
		if (mode == importConfig::TextureCompressionMode::Uncompressed)
		{
			hdrData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;
			return;
		}

		hdrData.compressionFormat = resource::TextureCompressionFormat::BC6H;

		for (auto& mip : hdrData.mipData)
		{
			// mip.data contains raw float32 RGBA pixels
			const float* floatData = reinterpret_cast<const float*>(mip.data.data());

			auto compressed = TextureCompressor::compressBC6H(
				floatData, mip.width, mip.height, quality);

			if (!compressed.empty())
			{
				mip.dataSize = static_cast<uint32_t>(compressed.size());
				mip.data = std::move(compressed);
			}
			else
			{
				vfLogError("BC6H compression failed for HDR mip {}x{}", mip.width, mip.height);
				hdrData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;
				return;
			}
		}

		vfLogDebug("Compressed HDR {}x{} to BC6H ({} mips)",
			hdrData.width, hdrData.height, hdrData.mipLevels);
	}

	// Generate a single mip level using 2x2 box filter (RGBA8)
	resource::MipLevelData Texture::generateMipLevel(const resource::MipLevelData& source) const
	{
		resource::MipLevelData result;
		result.width = std::max(1u, source.width / 2);
		result.height = std::max(1u, source.height / 2);

		size_t pixelCount = static_cast<size_t>(result.width) * result.height;
		result.data.resize(pixelCount * 4); // RGBA

		// Box filter: average 2x2 blocks of source pixels
		for (uint32_t y = 0; y < result.height; ++y)
		{
			for (uint32_t x = 0; x < result.width; ++x)
			{
				// Source coordinates (clamped for edge cases)
				uint32_t sx0 = std::min(x * 2, source.width - 1);
				uint32_t sy0 = std::min(y * 2, source.height - 1);
				uint32_t sx1 = std::min(x * 2 + 1, source.width - 1);
				uint32_t sy1 = std::min(y * 2 + 1, source.height - 1);

				// Sample 4 source pixels
				size_t idx00 = (static_cast<size_t>(sy0) * source.width + sx0) * 4;
				size_t idx10 = (static_cast<size_t>(sy0) * source.width + sx1) * 4;
				size_t idx01 = (static_cast<size_t>(sy1) * source.width + sx0) * 4;
				size_t idx11 = (static_cast<size_t>(sy1) * source.width + sx1) * 4;

				// Average each channel
				size_t dstIdx = (static_cast<size_t>(y) * result.width + x) * 4;
				for (int c = 0; c < 4; ++c)
				{
					uint32_t sum = static_cast<uint32_t>(source.data[idx00 + c]) +
								   static_cast<uint32_t>(source.data[idx10 + c]) +
								   static_cast<uint32_t>(source.data[idx01 + c]) +
								   static_cast<uint32_t>(source.data[idx11 + c]);
					result.data[dstIdx + c] = static_cast<unsigned char>((sum + 2) / 4); // +2 for rounding
				}
			}
		}

		return result;
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
