#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include "Types.hpp"

// Forward-declare stb_vorbis to avoid header pollution
struct stb_vorbis;

namespace resource {

	struct AudioStreamHeader {
		uint32_t sampleRate = 0;
		uint32_t channels = 0;
		uint32_t frames = 0;
		uint32_t totalDurationSeconds = 0;
		uint32_t dataSize = 0;
		AudioCompressionFormat compressionFormat = AudioCompressionFormat::PCM;
		AudioLoadType loadType = AudioLoadType::DecompressOnLoad;
		std::streampos dataStartOffset = 0;  // Position in file where data begins
	};

	class AudioStreamHandle {
	private:
		friend class AudioResource;

		std::ifstream file;
		AudioStreamHeader header;
		size_t currentSamplePosition = 0;

		// Vorbis streaming state
		std::vector<uint8_t> vorbisBuffer;   // Holds compressed data in memory
		stb_vorbis* vorbisHandle = nullptr;   // stb_vorbis decoder handle

		bool seekToSample(size_t sampleIndex);
		size_t getTotalSamples() const { return header.frames * header.channels; }

	public:
		explicit AudioStreamHandle() = default;
		~AudioStreamHandle();

		AudioStreamHandle(const AudioStreamHandle&) = delete;
		AudioStreamHandle& operator=(const AudioStreamHandle&) = delete;
		AudioStreamHandle(AudioStreamHandle&& other) noexcept;
		AudioStreamHandle& operator=(AudioStreamHandle&& other) noexcept;

		bool isOpen() const;
		size_t readSamples(std::vector<short>& buffer, size_t sampleCount);
		bool seekToTime(float seconds);
		void reset();
		bool isEOF() const;
		const AudioStreamHeader& getHeader() const { return header; }
		float getDuration() const { return static_cast<float>(header.totalDurationSeconds); }
	};

	class AudioResource
	{
	private:
		inline static const size_t chunkSize = 1024 * 1024;  // Chunk size for streaming (1MB)

	public:
		static AudioData loadAudio(std::string_view path);

		static std::unique_ptr<AudioStreamHandle> openStream(std::string_view path);
	};

}


