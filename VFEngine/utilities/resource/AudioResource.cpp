#include "AudioResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast

namespace resource {

	AudioData AudioResource::loadAudio(std::string_view path)
	{
		resource::AudioData audioData;

		// Validate input path
		if (path.empty()) {
			vfLogError("Empty path provided for audio loading");
			return {};
		}

		// Open the file in binary mode
		std::ifstream inFile(path.data(), std::ios::binary);
		if (!inFile) {
			vfLogError("Failed to open audio file for reading: {}", path);
			return {};
		}
		
		// Check file size to prevent loading extremely large files
		inFile.seekg(0, std::ios::end);
		auto filePos = inFile.tellg();
		inFile.seekg(0, std::ios::beg);
		
		if (filePos == std::ifstream::pos_type(-1)) {
			vfLogError("Failed to determine file size for: {}", path);
			return {};
		}
		
		std::streamsize fileSize = static_cast<std::streamsize>(filePos);
		if (fileSize > 500 * 1024 * 1024) { // 500MB limit
			vfLogError("Audio file {} is too large: {} bytes", path, static_cast<long long>(fileSize));
			return {};
		}

		// Read header file type (endian-safe)
		uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
		audioData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version information (endian-safe)
		uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
		uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

		// Validate version compatibility
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read audio metadata (endian-safe)
		audioData.sampleRate = endian::readLE<uint32_t>(inFile);
		audioData.channels = endian::readLE<uint32_t>(inFile);
		audioData.frames = endian::readLE<uint32_t>(inFile);
		audioData.totalDurationInSeconds = endian::readLE<uint32_t>(inFile);

		// Read the size of the raw audio data (endian-safe)
		uint32_t dataSize = endian::readLE<uint32_t>(inFile);
		
		// Validate data size
		if (dataSize == 0) {
			vfLogError("Audio file has zero data size: {}", path);
			return {};
		}
		
		if (dataSize > 400 * 1024 * 1024) { // 400MB limit for audio data
			vfLogError("Audio data size {} exceeds maximum limit", dataSize);
			return {};
		}
		
		// Validate that dataSize is reasonable given sample rate and channels
		if (audioData.sampleRate > 0 && audioData.channels > 0) {
			size_t expectedMaxSize = audioData.sampleRate * audioData.channels * sizeof(short) * 3600; // 1 hour max
			if (dataSize > expectedMaxSize) {
				vfLogError("Audio data size {} seems unreasonable for given parameters", dataSize);
				return {};
			}
		}

		size_t bytesRemaining = dataSize;

		audioData.data.reserve(dataSize / sizeof(short));  // Reserve space for the entire buffer
		size_t currentOffset = 0;

		// Read audio data in chunks (endian-safe)
		size_t totalSamples = dataSize / sizeof(short);
		endian::readVectorLE<short>(inFile, audioData.data, totalSamples);
		
		return audioData;
	}
}

