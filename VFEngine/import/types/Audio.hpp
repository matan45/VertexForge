#pragma once
#include <string>
#include <functional>
#include "config/Config.hpp"
#include "resource/Types.hpp"

namespace types {

	using AudioProgressCallback = std::function<void(float progress)>;

	class Audio
	{
	public:
		void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
		                  std::string_view location, AudioProgressCallback progressCallback = nullptr) const;
		void loadFromFileWithType(const importConfig::ImportFiles& file, std::string_view fileName,
		                          std::string_view location, std::string_view fileType,
		                          AudioProgressCallback progressCallback = nullptr) const;

	private:
		void loadOggFile(std::string_view path, std::string_view fileName, std::string_view location,
		                 AudioProgressCallback progressCallback) const;
		void loadWavFile(std::string_view path, std::string_view fileName, std::string_view location,
		                 AudioProgressCallback progressCallback) const;
		void loadMp3File(std::string_view path, std::string_view fileName, std::string_view location,
		                 AudioProgressCallback progressCallback) const;
		void saveToFile(std::string_view location, std::string_view fileName, const resource::AudioData& audioData) const;
	};
}

