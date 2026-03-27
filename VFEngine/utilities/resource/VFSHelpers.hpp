#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace resource
{
	// Read a file and parse as JSON (handles VFS + LZ4 decompression transparently)
	nlohmann::json readJsonFile(const std::string& path);

	// Read a file into raw bytes (handles VFS + LZ4 decompression transparently)
	std::vector<uint8_t> readFileBytes(const std::string& path);
}
