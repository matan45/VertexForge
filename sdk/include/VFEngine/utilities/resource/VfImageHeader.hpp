#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>

// VK-1659: the pixel size a .vfImage header stores — what a UI image needs for 9-slicing
// (UIImageComponent::sourceWidth / sourceHeight). Shared by the editor's UI image drawer and the MCP UI
// tools so the two read the header the same way.
//
//   off  size  field
//   ---  ----  ---------------------------------
//     0     1  fileType
//     1    12  version (3 x uint32)
//    13     4  width   (uint32)
//    17     4  height  (uint32)

namespace resource
{
    inline constexpr std::streamoff VF_IMAGE_SIZE_OFFSET = 13;

    // {width, height} from the header; nullopt when the file cannot be opened or is too short.
    inline std::optional<glm::uvec2> readVfImageSize(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.good())
            return std::nullopt;

        file.seekg(VF_IMAGE_SIZE_OFFSET, std::ios::beg);
        uint32_t w = 0, h = 0;
        file.read(reinterpret_cast<char*>(&w), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&h), sizeof(uint32_t));
        if (!file.good())
            return std::nullopt;
        return glm::uvec2(w, h);
    }
}
