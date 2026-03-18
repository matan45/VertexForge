#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <memory>
#include <mutex>
#include "Types.hpp"

namespace resource
{
    struct TextureMipFileInfo
    {
        std::streampos fileOffset = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t dataSize = 0;
    };

    struct TextureStreamHeader
    {
        FileType headerFileType = FileType::TEXTURE;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
        uint32_t mipLevels = 0;
        TextureCompressionFormat compression = TextureCompressionFormat::Uncompressed;
        std::vector<TextureMipFileInfo> mipInfos;
    };

    class TextureStreamHandle
    {
    private:
        std::ifstream file;
        TextureStreamHeader header;
        std::string filePath;
        mutable std::mutex fileMutex;

    public:
        TextureStreamHandle() = default;
        ~TextureStreamHandle();

        TextureStreamHandle(const TextureStreamHandle&) = delete;
        TextureStreamHandle& operator=(const TextureStreamHandle&) = delete;

        TextureStreamHandle(TextureStreamHandle&& other) noexcept;
        TextureStreamHandle& operator=(TextureStreamHandle&& other) noexcept;

        bool openStream(std::string_view path);
        void close();

        const TextureStreamHeader& getHeader() const { return header; }
        const std::string& getPath() const { return filePath; }

        bool readMipLevel(uint32_t level, MipLevelData& out);
        bool readMipRange(uint32_t fromLevel, uint32_t toLevel, std::vector<MipLevelData>& out);

    private:
        bool parseHeader();
    };

    class TextureStreamResource
    {
    public:
        static std::unique_ptr<TextureStreamHandle> openStream(std::string_view path);
    };
}
