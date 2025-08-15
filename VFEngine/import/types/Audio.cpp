#include "Audio.hpp"
#include "print/EditorLogger.hpp"
#include "../controllers/files/FileUtils.hpp"

#include <vector>
#include <fstream>
#include <bit>  // For std::bit_cast
#include <filesystem> 

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#include <stb_vorbis.c>



namespace types {
	void Audio::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName, std::string_view location) const
	{
		// File type detection is now handled by the pipeline, so we need to determine type from file extension
		std::string extension = files::FileUtils::getFileExtension(file.path.data());
		
		if (extension == ".ogg") {
			loadOggFile(file.path, fileName, location);
		}
		else if (extension == ".wav") {
			loadWavFile(file.path, fileName, location);
		}
		else if (extension == ".mp3") {
			loadMp3File(file.path, fileName, location);
		}
		else {
			vfLogError("Unsupported audio file extension: {}", extension);
		}
	}

	void Audio::loadFromFileWithType(const importConfig::ImportFiles& file, std::string_view fileName, std::string_view location, std::string_view fileType) const
	{
		if (fileType == "OGG") {
			loadOggFile(file.path, fileName, location);
		}
		else if (fileType == "WAV") {
			loadWavFile(file.path, fileName, location);
		}
		else if (fileType == "MP3") {
			loadMp3File(file.path, fileName, location);
		}
		else {
			vfLogError("Unsupported audio file type: {}", fileType);
		}
	}

	void Audio::loadOggFile(std::string_view path, std::string_view fileName, std::string_view location) const
	{
		resource::AudioData audioData;
		audioData.headerFileType = resource::FileType::AUDIO;
		// Open and load Ogg Vorbis file using stb_vorbis
		int error;
		stb_vorbis* vorbis = stb_vorbis_open_filename(path.data(), &error, nullptr);
		if (!vorbis) {
			vfLogError("Failed to load Ogg Vorbis file: {}", path);
			return;
		}

		// Retrieve file information
		stb_vorbis_info info = stb_vorbis_get_info(vorbis);
		audioData.sampleRate = info.sample_rate;
		audioData.channels = info.channels;

		// Get the total number of samples
		int frames = stb_vorbis_stream_length_in_samples(vorbis);
		int totalSamples = frames * info.channels;
		audioData.frames = frames;
		audioData.totalDurationInSeconds = static_cast<uint32_t>(stb_vorbis_stream_length_in_seconds(vorbis));

		// Resize the data buffer and read samples
		audioData.data.resize(totalSamples);
		stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, audioData.data.data(), totalSamples);

		saveToFile(location, fileName, audioData);

		// Cleanup
		stb_vorbis_close(vorbis);
	}

	void Audio::loadWavFile(std::string_view path, std::string_view fileName, std::string_view location) const
	{
		resource::AudioData audioData;
		audioData.headerFileType = resource::FileType::AUDIO;
		// Open and load WAV file using dr_wav
		drwav wav;
		if (!drwav_init_file(&wav, path.data(), nullptr)) {
			vfLogError("Failed to load WAV file: {}", path );
			return ;
		}

		// Set up the AudioData structure
		audioData.sampleRate = wav.sampleRate;
		audioData.channels = wav.channels;
		audioData.frames = static_cast<uint32_t>(wav.totalPCMFrameCount);
		audioData.totalDurationInSeconds = static_cast<uint32_t>(wav.totalPCMFrameCount / wav.sampleRate);

		// Load WAV data into the vector (16-bit signed samples)
		audioData.data.resize(wav.totalPCMFrameCount * wav.channels);
		drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, audioData.data.data());

		saveToFile(location, fileName, audioData);

		// Cleanup
		drwav_uninit(&wav);
	}

	void Audio::loadMp3File(std::string_view path, std::string_view fileName, std::string_view location) const
	{
		resource::AudioData audioData;
		audioData.headerFileType = resource::FileType::AUDIO;

		// Open and load MP3 file using dr_mp3
		drmp3 mp3;
		if (!drmp3_init_file(&mp3, path.data(), nullptr)) {
			vfLogError("Failed to load MP3 file: {}" , path);
			return ;
		}

		// Set up the AudioData structure
		audioData.sampleRate = mp3.sampleRate;
		audioData.channels = mp3.channels;

		// Read the MP3 data into a temporary buffer
		drmp3_uint64 totalFrames = drmp3_get_pcm_frame_count(&mp3);
		audioData.frames = static_cast<uint32_t>(totalFrames);
		audioData.totalDurationInSeconds = static_cast<uint32_t>(totalFrames / mp3.sampleRate);

		// Resize the audio data buffer and read into it
		audioData.data.resize(totalFrames * mp3.channels);
		drmp3_read_pcm_frames_s16(&mp3, totalFrames, audioData.data.data());

		saveToFile(location, fileName, audioData);

		// Cleanup
		drmp3_uninit(&mp3);
	}

	void Audio::saveToFile(std::string_view location, std::string_view fileName, const resource::AudioData& audioData) const
	{
		// Open the file in binary mode
		std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." + FileExtension::audio);
		std::ofstream outFile(newFileLocation, std::ios::binary);
		if (!outFile) {
			vfLogError("Failed to open file for writing: {}" ,newFileLocation.string());
			return;
		}

		// Write version
		// Write the header file type
		uint8_t headerFileType = static_cast<uint8_t>(audioData.headerFileType);
		outFile.write(std::bit_cast<const char*>(&headerFileType), sizeof(headerFileType));
		
		// Serialize the mesh data (this is just an example, adapt to your format)
		uint32_t majorVersion = std::bit_cast<uint32_t>(Version::major);
		uint32_t minorVersion = std::bit_cast<uint32_t>(Version::minor);
		uint32_t patchVersion = std::bit_cast<uint32_t>(Version::patch);
		outFile.write(std::bit_cast<const char*>(&majorVersion), sizeof(majorVersion));
		outFile.write(std::bit_cast<const char*>(&minorVersion), sizeof(minorVersion));
		outFile.write(std::bit_cast<const char*>(&patchVersion), sizeof(patchVersion));

		// Write sample rate, channels, frames, and total duration
		outFile.write(std::bit_cast<const char*>(&audioData.sampleRate), sizeof(audioData.sampleRate));
		outFile.write(std::bit_cast<const char*>(&audioData.channels), sizeof(audioData.channels));
		outFile.write(std::bit_cast<const char*>(&audioData.frames), sizeof(audioData.frames));
		outFile.write(std::bit_cast<const char*>(&audioData.totalDurationInSeconds), sizeof(audioData.totalDurationInSeconds));

		// Write audio data size and the raw audio data
		auto dataSize = static_cast<uint32_t>(audioData.data.size());
		outFile.write(std::bit_cast<const char*>(&dataSize), sizeof(dataSize));
		outFile.write(std::bit_cast<const char*>(audioData.data.data()), dataSize);

		outFile.close();
	}
}

