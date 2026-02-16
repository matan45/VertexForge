#pragma once
#include <string>
#include <functional>
#include <vector>
#include "config/Config.hpp"
#include "resource/Types.hpp"

namespace types {

	using AudioProgressCallback = std::function<void(float progress)>;

	struct DecodedAudio
	{
		uint32_t sampleRate = 0;
		uint32_t channels = 0;
		uint32_t frames = 0;
		uint32_t totalDurationInSeconds = 0;
		std::vector<short> data;
	};

	class Audio
	{
	public:
		void loadFromFileWithType(const importConfig::ImportFiles& file, std::string_view fileName,
		                          std::string_view location, std::string_view fileType,
		                          AudioProgressCallback progressCallback = nullptr) const;

	private:
		DecodedAudio decodeOgg(std::string_view path) const;
		DecodedAudio decodeWav(std::string_view path) const;
		DecodedAudio decodeMp3(std::string_view path) const;
		void saveToFile(std::string_view location, std::string_view fileName, const resource::AudioData& audioData) const;
	};
}

