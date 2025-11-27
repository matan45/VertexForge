#pragma once
#include <string>
#include "config/Config.hpp"
#include "resource/Types.hpp"

namespace types {
	class Audio
	{
	public:
		void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName, std::string_view location) const;
		void loadFromFileWithType(const importConfig::ImportFiles& file, std::string_view fileName, std::string_view location, std::string_view fileType) const;

	private:
		void loadOggFile(std::string_view path, std::string_view fileName, std::string_view location) const;
		void loadWavFile(std::string_view path, std::string_view fileName, std::string_view location) const;
		void loadMp3File(std::string_view path, std::string_view fileName, std::string_view location) const;
		void saveToFile(std::string_view location, std::string_view fileName, const resource::AudioData& audioData) const;
	};
}

