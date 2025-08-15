#include "AudioResource.hpp"
#include "../print/EditorLogger.hpp"

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

		// Read version
		// Read the header file type
		uint8_t headerFileType;
		inFile.read(std::bit_cast<char*>(&headerFileType), sizeof(headerFileType));
		audioData.headerFileType = static_cast<resource::FileType>(headerFileType);

		// Read version
		// Read the version information
		uint32_t majorVersion, minorVersion, patchVersion;
		inFile.read(std::bit_cast<char*>(&majorVersion), sizeof(majorVersion));
		inFile.read(std::bit_cast<char*>(&minorVersion), sizeof(minorVersion));
		inFile.read(std::bit_cast<char*>(&patchVersion), sizeof(patchVersion));

		// Validate version compatibility
		if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch) {
			vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
			return {};
		}

		// Read audio metadata: sample rate, channels, frames, total duration
		inFile.read(std::bit_cast<char*>(&audioData.sampleRate), sizeof(audioData.sampleRate));
		inFile.read(std::bit_cast<char*>(&audioData.channels), sizeof(audioData.channels));
		inFile.read(std::bit_cast<char*>(&audioData.frames), sizeof(audioData.frames));
		inFile.read(std::bit_cast<char*>(&audioData.totalDurationInSeconds), sizeof(audioData.totalDurationInSeconds));

		// Read the size of the raw audio data
		uint32_t dataSize;
		inFile.read(std::bit_cast<char*>(&dataSize), sizeof(dataSize));
		
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

		while (bytesRemaining > 0) {
			size_t bytesToRead = std::min(chunkSize, bytesRemaining);

			// Read chunk
			std::vector<short> buffer(bytesToRead / sizeof(short));
			inFile.read(std::bit_cast<char*>(buffer.data()), bytesToRead);

			// Process or play this chunk (e.g., feeding it to an audio buffer)
			audioData.data.insert(audioData.data.end(), buffer.begin(), buffer.end());

			currentOffset += bytesToRead;
			bytesRemaining -= bytesToRead;
		}
		
		return audioData;
	}
}

