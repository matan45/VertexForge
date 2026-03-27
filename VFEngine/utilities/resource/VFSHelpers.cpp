#include "VFSHelpers.hpp"
#include "VirtualFileSystem.hpp"
#include "../print/Log.hpp"

namespace resource
{
	nlohmann::json readJsonFile(const std::string& path)
	{
		auto data = VirtualFileSystem::instance().readFile(path);
		if (data.empty())
		{
			// Try to open directly from filesystem as fallback
			std::ifstream file(path);
			if (!file.is_open())
			{
				vfLogError("readJsonFile: Failed to read file: {}", path);
				return nlohmann::json{};
			}
			return nlohmann::json::parse(file);
		}

		return nlohmann::json::parse(data.begin(), data.end());
	}

	std::vector<uint8_t> readFileBytes(const std::string& path)
	{
		return VirtualFileSystem::instance().readFile(path);
	}
}
