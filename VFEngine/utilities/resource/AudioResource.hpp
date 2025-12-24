#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include "Types.hpp"


namespace resource {

	// Header information for streaming audio
	struct AudioStreamHeader {
		uint32_t sampleRate = 0;
		uint32_t channels = 0;
		uint32_t frames = 0;
		uint32_t totalDurationSeconds = 0;
		uint32_t dataSize = 0;
		std::streampos dataStartOffset = 0;  // Position in file where PCM data begins
	};

	// Handle for streaming audio - keeps file open for chunked reading
	class AudioStreamHandle {
	private:
		friend class AudioResource;

		std::ifstream file;
		AudioStreamHeader header;
		size_t currentSamplePosition = 0;
	public:
		explicit AudioStreamHandle() = default;
		~AudioStreamHandle();

		// Non-copyable, movable
		AudioStreamHandle(const AudioStreamHandle&) = delete;
		AudioStreamHandle& operator=(const AudioStreamHandle&) = delete;
		AudioStreamHandle(AudioStreamHandle&& other) noexcept;
		AudioStreamHandle& operator=(AudioStreamHandle&& other) noexcept;

		// Check if stream is open and valid
		[[nodiscard]] bool isOpen() const { return file.is_open(); }

		// Read chunk of samples into buffer, returns actual samples read
		// sampleCount is total samples (frames * channels)
		size_t readSamples(std::vector<short>& buffer, size_t sampleCount);

		// Seek to sample position (0-based, total samples not frames)
		bool seekToSample(size_t sampleIndex);

		// Seek to time position in seconds
		bool seekToTime(float seconds);

		// Reset to beginning of audio data (for looping)
		void reset();

		// Check if at end of file
		[[nodiscard]] bool isEOF() const;

		// Get header information
		[[nodiscard]] const AudioStreamHeader& getHeader() const { return header; }

		// Get current sample position
		[[nodiscard]] size_t getCurrentSamplePosition() const { return currentSamplePosition; }

		// Get current time position in seconds
		[[nodiscard]] float getCurrentTimePosition() const;

		// Get total samples in file
		[[nodiscard]] size_t getTotalSamples() const { return header.frames * header.channels; }

		// Get duration in seconds
		[[nodiscard]] float getDuration() const { return static_cast<float>(header.totalDurationSeconds); }

	
	};

	class AudioResource
	{
	private:
		inline static const size_t chunkSize = 1024 * 1024;  // Chunk size for streaming (1MB)

	public:
		// Load entire audio file into memory (existing behavior)
		static AudioData loadAudio(std::string_view path);

		// Open audio file for streaming (reads header only, keeps file open)
		static std::unique_ptr<AudioStreamHandle> openStream(std::string_view path);
	};

}


