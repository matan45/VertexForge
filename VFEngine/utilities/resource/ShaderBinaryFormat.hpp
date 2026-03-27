#pragma once

#include <cstdint>
#include <vector>
#include <filesystem>
#include <fstream>
#include <string>
#include "print/Log.hpp"

namespace resource
{
	struct StageSPIRV
	{
		uint32_t stage;
		std::vector<uint32_t> spirv;
	};

	class ShaderBinaryFormat
	{
	public:
		static constexpr uint32_t MAGIC = 0x48534656; // "VFSH" in little-endian
		static constexpr uint32_t VERSION = 1;

		static bool write(const std::filesystem::path& path, const std::vector<StageSPIRV>& stages)
		{
			std::ofstream file(path, std::ios::binary);
			if (!file.is_open())
			{
				vfLogError("ShaderBinaryFormat: Failed to open file for writing: {}", path.string());
				return false;
			}

			uint32_t magic = MAGIC;
			uint32_t version = VERSION;
			uint32_t stageCount = static_cast<uint32_t>(stages.size());

			file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
			file.write(reinterpret_cast<const char*>(&version), sizeof(version));
			file.write(reinterpret_cast<const char*>(&stageCount), sizeof(stageCount));

			for (const auto& stage : stages)
			{
				uint32_t stageFlag = stage.stage;
				uint32_t byteCount = static_cast<uint32_t>(stage.spirv.size() * sizeof(uint32_t));

				file.write(reinterpret_cast<const char*>(&stageFlag), sizeof(stageFlag));
				file.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
				file.write(reinterpret_cast<const char*>(stage.spirv.data()), byteCount);
			}

			return file.good();
		}

		static std::vector<StageSPIRV> read(const std::filesystem::path& path)
		{
			std::vector<StageSPIRV> stages;

			std::ifstream file(path, std::ios::binary);
			if (!file.is_open())
			{
				vfLogError("ShaderBinaryFormat: Failed to open file: {}", path.string());
				return stages;
			}

			uint32_t magic = 0;
			uint32_t version = 0;
			uint32_t stageCount = 0;

			file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
			file.read(reinterpret_cast<char*>(&version), sizeof(version));
			file.read(reinterpret_cast<char*>(&stageCount), sizeof(stageCount));

			if (magic != MAGIC)
			{
				vfLogError("ShaderBinaryFormat: Invalid magic number in {}", path.string());
				return stages;
			}

			if (version != VERSION)
			{
				vfLogError("ShaderBinaryFormat: Unsupported version {} in {}", version, path.string());
				return stages;
			}

			stages.reserve(stageCount);
			for (uint32_t i = 0; i < stageCount; ++i)
			{
				StageSPIRV stage{};
				uint32_t byteCount = 0;

				file.read(reinterpret_cast<char*>(&stage.stage), sizeof(stage.stage));
				file.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));

				if (!file.good() || byteCount % sizeof(uint32_t) != 0)
				{
					vfLogError("ShaderBinaryFormat: Corrupt stage data in {}", path.string());
					return {};
				}

				stage.spirv.resize(byteCount / sizeof(uint32_t));
				file.read(reinterpret_cast<char*>(stage.spirv.data()), byteCount);

				if (!file.good())
				{
					vfLogError("ShaderBinaryFormat: Truncated SPIR-V data in {}", path.string());
					return {};
				}

				stages.push_back(std::move(stage));
			}

			return stages;
		}
	};
}
