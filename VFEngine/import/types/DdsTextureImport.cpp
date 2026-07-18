#include "Texture.hpp"
#include "BlockTextureAssembly.hpp"
#include "ImportDecodeGuard.hpp"
#include "print/Log.hpp"
#include "cpumem/ScopedCpuMemory.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>

// VK-1642: DDS container import. Parses the DDS_HEADER (+ optional DX10 header),
// then reuses the shared block-decode helpers: BC7/BC6H pass through verbatim,
// BC1-5 decode to RGBA8 via bcdec, and uncompressed RGBA8/BGRA8/float are read
// directly. Only single-image 2D textures are supported.
namespace types
{
    namespace
    {
        bool readWholeFileDds(std::string_view path, std::vector<uint8_t>& out)
        {
            std::ifstream in(std::string(path), std::ios::binary | std::ios::ate);
            if (!in)
                return false;
            const std::streamoff size = in.tellg();
            if (size <= 0)
                return false;
            out.resize(static_cast<size_t>(size));
            in.seekg(0, std::ios::beg);
            in.read(reinterpret_cast<char*>(out.data()), size);
            return static_cast<bool>(in);
        }

        // DDS is always little-endian on disk.
        uint32_t rdLE(const unsigned char* p)
        {
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }

        constexpr uint32_t fourcc(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
                   (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
        }

        constexpr uint32_t kDdsMagic = fourcc('D', 'D', 'S', ' ');
        constexpr uint32_t kFourCC_DXT1 = fourcc('D', 'X', 'T', '1');
        constexpr uint32_t kFourCC_DXT3 = fourcc('D', 'X', 'T', '3');
        constexpr uint32_t kFourCC_DXT5 = fourcc('D', 'X', 'T', '5');
        constexpr uint32_t kFourCC_ATI1 = fourcc('A', 'T', 'I', '1');
        constexpr uint32_t kFourCC_BC4U = fourcc('B', 'C', '4', 'U');
        constexpr uint32_t kFourCC_BC4S = fourcc('B', 'C', '4', 'S');
        constexpr uint32_t kFourCC_ATI2 = fourcc('A', 'T', 'I', '2');
        constexpr uint32_t kFourCC_BC5U = fourcc('B', 'C', '5', 'U');
        constexpr uint32_t kFourCC_BC5S = fourcc('B', 'C', '5', 'S');
        constexpr uint32_t kFourCC_DX10 = fourcc('D', 'X', '1', '0');

        constexpr uint32_t DDPF_FOURCC = 0x4;

        // DXGI_FORMAT subset (values from dxgiformat.h).
        enum : uint32_t
        {
            DXGI_R32G32B32A32_FLOAT = 2,
            DXGI_R16G16B16A16_FLOAT = 10,
            DXGI_R8G8B8A8_UNORM = 28,
            DXGI_R8G8B8A8_UNORM_SRGB = 29,
            DXGI_BC1_UNORM = 71,
            DXGI_BC1_UNORM_SRGB = 72,
            DXGI_BC2_UNORM = 74,
            DXGI_BC2_UNORM_SRGB = 75,
            DXGI_BC3_UNORM = 77,
            DXGI_BC3_UNORM_SRGB = 78,
            DXGI_BC4_UNORM = 80,
            DXGI_BC4_SNORM = 81,
            DXGI_BC5_UNORM = 83,
            DXGI_BC5_SNORM = 84,
            DXGI_B8G8R8A8_UNORM = 87,
            DXGI_B8G8R8A8_UNORM_SRGB = 91,
            DXGI_BC6H_UF16 = 95,
            DXGI_BC6H_SF16 = 96,
            DXGI_BC7_UNORM = 98,
            DXGI_BC7_UNORM_SRGB = 99,
        };

        enum class DdsMode { Unsupported, PassBC7, PassBC6H, DecodeBC, Rgba8, Bgra8, Float16, Float32 };

        DdsMode classifyDxgi(uint32_t dxgi, BcKind& bcOut)
        {
            switch (dxgi)
            {
            case DXGI_BC1_UNORM: case DXGI_BC1_UNORM_SRGB: bcOut = BcKind::BC1; return DdsMode::DecodeBC;
            case DXGI_BC2_UNORM: case DXGI_BC2_UNORM_SRGB: bcOut = BcKind::BC2; return DdsMode::DecodeBC;
            case DXGI_BC3_UNORM: case DXGI_BC3_UNORM_SRGB: bcOut = BcKind::BC3; return DdsMode::DecodeBC;
            case DXGI_BC4_UNORM: case DXGI_BC4_SNORM: bcOut = BcKind::BC4_UNORM; return DdsMode::DecodeBC;
            case DXGI_BC5_UNORM: case DXGI_BC5_SNORM: bcOut = BcKind::BC5_UNORM; return DdsMode::DecodeBC;
            case DXGI_BC6H_UF16: case DXGI_BC6H_SF16: return DdsMode::PassBC6H;
            case DXGI_BC7_UNORM: case DXGI_BC7_UNORM_SRGB: return DdsMode::PassBC7;
            case DXGI_R8G8B8A8_UNORM: case DXGI_R8G8B8A8_UNORM_SRGB: return DdsMode::Rgba8;
            case DXGI_B8G8R8A8_UNORM: case DXGI_B8G8R8A8_UNORM_SRGB: return DdsMode::Bgra8;
            case DXGI_R16G16B16A16_FLOAT: return DdsMode::Float16;
            case DXGI_R32G32B32A32_FLOAT: return DdsMode::Float32;
            default: return DdsMode::Unsupported;
            }
        }

        // Bytes of one mip level on disk for the given mode/dimensions.
        uint32_t ddsLevelBytes(DdsMode mode, BcKind bcKind, uint32_t lw, uint32_t lh)
        {
            switch (mode)
            {
            case DdsMode::PassBC7:
            case DdsMode::PassBC6H:
                return ((lw + 3) / 4) * ((lh + 3) / 4) * 16u;
            case DdsMode::DecodeBC:
                return bcLevelByteSize(lw, lh, bcKind);
            case DdsMode::Rgba8:
            case DdsMode::Bgra8:
                return lw * lh * 4u;
            case DdsMode::Float16:
                return lw * lh * 8u;
            case DdsMode::Float32:
                return lw * lh * 16u;
            default:
                return 0;
            }
        }

        DecodedTexture decodeDds(const std::vector<uint8_t>& bytes)
        {
            DecodedTexture result;

            if (bytes.size() < 128 || rdLE(&bytes[0]) != kDdsMagic)
            {
                vfLogError("DDS: not a DDS file or truncated header");
                return result;
            }

            const uint32_t height = rdLE(&bytes[12]);
            const uint32_t width = rdLE(&bytes[16]);
            uint32_t mipCount = rdLE(&bytes[28]);
            const uint32_t pfFlags = rdLE(&bytes[80]);
            const uint32_t fourCC = rdLE(&bytes[84]);
            const uint32_t rgbBitCount = rdLE(&bytes[88]);
            const uint32_t rMask = rdLE(&bytes[92]);
            const uint32_t gMask = rdLE(&bytes[96]);
            const uint32_t bMask = rdLE(&bytes[100]);
            const uint32_t aMask = rdLE(&bytes[104]);

            if (width == 0 || height == 0)
            {
                vfLogError("DDS: zero dimension");
                return result;
            }
            if (mipCount == 0)
                mipCount = 1;

            BcKind bcKind = BcKind::BC1;
            DdsMode mode = DdsMode::Unsupported;
            size_t dataOffset = 128;

            const bool hasFourCC = (pfFlags & DDPF_FOURCC) != 0;
            if (hasFourCC && fourCC == kFourCC_DX10)
            {
                if (bytes.size() < 148)
                {
                    vfLogError("DDS: truncated DX10 header");
                    return result;
                }
                const uint32_t dxgi = rdLE(&bytes[128]);
                const uint32_t arraySize = rdLE(&bytes[140]);
                if (arraySize > 1)
                {
                    vfLogWarning("DDS: texture arrays are not supported for import; skipping");
                    return result;
                }
                mode = classifyDxgi(dxgi, bcKind);
                dataOffset = 148;
                if (mode == DdsMode::Unsupported)
                    vfLogWarning("DDS: unsupported DXGI format {}; skipping", dxgi);
            }
            else if (hasFourCC)
            {
                switch (fourCC)
                {
                case kFourCC_DXT1: bcKind = BcKind::BC1; mode = DdsMode::DecodeBC; break;
                case kFourCC_DXT3: bcKind = BcKind::BC2; mode = DdsMode::DecodeBC; break;
                case kFourCC_DXT5: bcKind = BcKind::BC3; mode = DdsMode::DecodeBC; break;
                case kFourCC_ATI1: case kFourCC_BC4U: case kFourCC_BC4S:
                    bcKind = BcKind::BC4_UNORM; mode = DdsMode::DecodeBC; break;
                case kFourCC_ATI2: case kFourCC_BC5U: case kFourCC_BC5S:
                    bcKind = BcKind::BC5_UNORM; mode = DdsMode::DecodeBC; break;
                default:
                    vfLogWarning("DDS: unsupported FourCC 0x{:X}; skipping", fourCC);
                    return result;
                }
            }
            else
            {
                // Uncompressed via channel masks. Handle the two common 32bpp layouts.
                if (rgbBitCount == 32 && aMask == 0xFF000000u)
                {
                    if (rMask == 0x00FF0000u && bMask == 0x000000FFu)
                        mode = DdsMode::Bgra8; // A8R8G8B8 (classic D3DFMT)
                    else if (rMask == 0x000000FFu && bMask == 0x00FF0000u)
                        mode = DdsMode::Rgba8; // A8B8G8R8
                }
                if (mode == DdsMode::Unsupported)
                {
                    vfLogWarning("DDS: unsupported uncompressed layout (bits={}, R=0x{:X} A=0x{:X}); skipping",
                                 rgbBitCount, rMask, aMask);
                }
            }

            if (mode == DdsMode::Unsupported)
                return result;

            result.width = width;
            result.height = height;
            switch (mode)
            {
            case DdsMode::PassBC7: result.payload = DecodedTexture::Payload::BcBlocks;
                                   result.blockFormat = resource::TextureCompressionFormat::BC7; break;
            case DdsMode::PassBC6H: result.payload = DecodedTexture::Payload::BcBlocks;
                                    result.blockFormat = resource::TextureCompressionFormat::BC6H;
                                    result.isHdr = true; break;
            case DdsMode::Float16:
            case DdsMode::Float32: result.payload = DecodedTexture::Payload::Float; result.isHdr = true; break;
            default: result.payload = DecodedTexture::Payload::Rgba8; break;
            }

            size_t offset = dataOffset;
            result.levels.reserve(mipCount);

            for (uint32_t level = 0; level < mipCount; ++level)
            {
                const uint32_t lw = std::max(1u, width >> level);
                const uint32_t lh = std::max(1u, height >> level);
                const uint32_t levelBytes = ddsLevelBytes(mode, bcKind, lw, lh);

                if (levelBytes == 0 || offset + levelBytes > bytes.size())
                {
                    vfLogError("DDS: truncated level {} (need {} bytes at offset {}, have {})",
                               level, levelBytes, offset, bytes.size());
                    return DecodedTexture{};
                }
                const unsigned char* levelData = &bytes[offset];

                resource::MipLevelData mip;
                switch (mode)
                {
                case DdsMode::PassBC7:
                case DdsMode::PassBC6H:
                    mip.width = lw;
                    mip.height = lh;
                    mip.dataSize = levelBytes;
                    mip.data.assign(levelData, levelData + levelBytes);
                    break;

                case DdsMode::DecodeBC:
                {
                    auto rgba = decodeBcToRgba8(levelData, lw, lh, bcKind);
                    if (rgba.empty())
                    {
                        vfLogError("DDS: BC decode failed (level {})", level);
                        return DecodedTexture{};
                    }
                    mip.width = lw;
                    mip.height = lh;
                    mip.data = std::move(rgba);
                    break;
                }

                case DdsMode::Rgba8:
                    mip.width = lw;
                    mip.height = lh;
                    mip.data.assign(levelData, levelData + static_cast<size_t>(lw) * lh * 4);
                    break;

                case DdsMode::Bgra8:
                {
                    mip.width = lw;
                    mip.height = lh;
                    const size_t pixels = static_cast<size_t>(lw) * lh;
                    mip.data.resize(pixels * 4);
                    for (size_t i = 0; i < pixels; ++i)
                    {
                        mip.data[i * 4 + 0] = levelData[i * 4 + 2];
                        mip.data[i * 4 + 1] = levelData[i * 4 + 1];
                        mip.data[i * 4 + 2] = levelData[i * 4 + 0];
                        mip.data[i * 4 + 3] = levelData[i * 4 + 3];
                    }
                    break;
                }

                case DdsMode::Float16:
                {
                    const size_t components = static_cast<size_t>(lw) * lh * 4;
                    mip.width = lw;
                    mip.height = lh;
                    mip.data.resize(components * sizeof(float));
                    const uint16_t* s = reinterpret_cast<const uint16_t*>(levelData);
                    float* d = reinterpret_cast<float*>(mip.data.data());
                    for (size_t i = 0; i < components; ++i)
                        d[i] = halfToFloat(s[i]);
                    mip.dataSize = static_cast<uint32_t>(mip.data.size());
                    break;
                }

                case DdsMode::Float32:
                    mip.width = lw;
                    mip.height = lh;
                    mip.data.assign(levelData, levelData + levelBytes);
                    mip.dataSize = levelBytes;
                    break;

                default:
                    return DecodedTexture{};
                }

                result.levels.push_back(std::move(mip));
                offset += levelBytes;
            }

            result.ok = true;
            return result;
        }
    }

    TextureImportResult Texture::loadDdsFile(const importConfig::ImportFiles& file, std::string_view fileName,
                                             std::string_view location, TextureProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        std::vector<uint8_t> bytes;
        if (!readWholeFileDds(file.path, bytes) || bytes.size() < 128)
        {
            vfLogError("DDS: cannot read '{}'", file.path);
            return TextureImportResult::Failed;
        }

        ImportDecodeLock decodeLock;
        memory::ScopedCpuMemory decodeGuard(texDecodeCategory(), static_cast<uint64_t>(bytes.size()) * 6u);

        DecodedTexture decoded = decodeDds(bytes);

        if (progressCallback) progressCallback(0.6f);
        const TextureImportResult res = writeDecodedTexture(std::move(decoded), fileName, location, file.config);
        if (progressCallback) progressCallback(1.0f);
        return res;
    }
}
