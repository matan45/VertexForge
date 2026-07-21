#include "Texture.hpp"
#include "BlockTextureAssembly.hpp"
#include "ImportDecodeGuard.hpp"
#include "print/Log.hpp"
#include "cpumem/ScopedCpuMemory.hpp"

// This is the ONLY translation unit that includes the heavy Basis Universal
// transcoder. basisu types never cross VF_IMPORT_API or enter another TU's
// include graph (VK-1642). Compiled/linked via the `basisu` StaticLib. Pin the
// same config as that lib: KTX2 on, Zstd off (the real zstd amalgamation is not
// vendored). Neither define affects the transcoder class layout, but pinning
// keeps this TU's view identical regardless of project-level defines.
#define BASISD_SUPPORT_KTX2 1
#define BASISD_SUPPORT_KTX2_ZSTD 0
#include <basisu_transcoder.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>
#include <algorithm>

namespace types
{
    namespace
    {
        // 12-byte KTX container identifiers (differ only at bytes 5-6: "20" vs "11").
        constexpr unsigned char kKtx2Id[12] =
            {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        constexpr unsigned char kKtx1Id[12] =
            {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

        bool readWholeFile(std::string_view path, std::vector<uint8_t>& out)
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

        uint32_t readU32(const unsigned char* p, bool bigEndian)
        {
            if (bigEndian)
                return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }

        // ---------------------------- KTX2 (Basis Universal) ----------------------------

        DecodedTexture decodeKtx2(const std::vector<uint8_t>& bytes, const importConfig::ImportConfig& config)
        {
            DecodedTexture result;

            static std::once_flag initFlag;
            std::call_once(initFlag, [] { basist::basisu_transcoder_init(); });

            basist::ktx2_transcoder tc;
            if (!tc.init(bytes.data(), static_cast<uint32_t>(bytes.size())))
            {
                vfLogError("KTX2: failed to parse container header");
                return result;
            }

            // Only 2D single-image textures are supported for import.
            if (tc.get_faces() > 1 || tc.get_layers() > 1)
            {
                vfLogWarning("KTX2: cubemaps / texture arrays are not supported for import "
                             "(faces={}, layers={}); skipping", tc.get_faces(), tc.get_layers());
                return result;
            }

            if (!tc.start_transcoding())
            {
                vfLogError("KTX2: start_transcoding failed (Zstd-supercompressed UASTC is unsupported)");
                return result;
            }

            const bool isHdr = tc.is_hdr();
            const bool wantBc = (config.compressionMode == importConfig::TextureCompressionMode::BC);
            const uint32_t levels = tc.get_levels();
            result.width = tc.get_width();
            result.height = tc.get_height();
            result.isHdr = isHdr;

            // Defensive bounds: reject implausible dimensions/levels so a malformed
            // container that still parses can't drive a huge allocation.
            if (result.width == 0 || result.height == 0 ||
                result.width > 65536 || result.height > 65536 || levels == 0 || levels > 24)
            {
                vfLogWarning("KTX2: implausible dimensions {}x{} ({} levels); skipping",
                             result.width, result.height, levels);
                return DecodedTexture{};
            }

            vfLogDebug("KTX2: {}x{}, {} level(s), {}, srgb={}", result.width, result.height, levels,
                       isHdr ? "HDR" : "LDR", tc.is_srgb());

            // Direct transcode target: BC7 (LDR) / BC6H (HDR) for the default BC path;
            // RGBA32 / RGBA_HALF when the config forces uncompressed output.
            basist::transcoder_texture_format target;
            if (isHdr)
                target = wantBc ? basist::transcoder_texture_format::cTFBC6H
                                : basist::transcoder_texture_format::cTFRGBA_HALF;
            else
                target = wantBc ? basist::transcoder_texture_format::cTFBC7_RGBA
                                : basist::transcoder_texture_format::cTFRGBA32;

            const bool bcTarget = (target == basist::transcoder_texture_format::cTFBC7_RGBA ||
                                   target == basist::transcoder_texture_format::cTFBC6H);

            basist::ktx2_transcoder_state state; // stack-local => thread-safe under concurrent imports
            state.clear();                        // init m_uncomp_data_level_index (raw int) to -1
            result.levels.reserve(levels);

            for (uint32_t level = 0; level < levels; ++level)
            {
                basist::ktx2_image_level_info info;
                if (!tc.get_image_level_info(info, level, 0, 0))
                {
                    vfLogError("KTX2: get_image_level_info failed (level {})", level);
                    return DecodedTexture{};
                }
                const uint32_t lw = info.m_orig_width;
                const uint32_t lh = info.m_orig_height;

                // Output units: 4x4 output blocks for BC targets, pixels otherwise.
                uint32_t outUnits;
                uint32_t byteSize;
                if (bcTarget)
                {
                    const uint32_t bx = (lw + 3) / 4;
                    const uint32_t by = (lh + 3) / 4;
                    outUnits = bx * by;
                    byteSize = outUnits * 16; // 16 bytes/block for BC7 and BC6H
                }
                else if (target == basist::transcoder_texture_format::cTFRGBA_HALF)
                {
                    outUnits = lw * lh;
                    byteSize = outUnits * 8; // 4 half-float components
                }
                else // cTFRGBA32
                {
                    outUnits = lw * lh;
                    byteSize = outUnits * 4;
                }

                resource::MipLevelData mip;
                mip.width = lw;
                mip.height = lh;
                mip.dataSize = byteSize;
                mip.data.resize(byteSize);

                if (!tc.transcode_image_level(level, 0, 0, mip.data.data(), outUnits, target,
                                              0, 0, 0, -1, -1, &state))
                {
                    vfLogWarning("KTX2: transcode failed (level {}, target {})", level,
                                 static_cast<int>(target));
                    return DecodedTexture{};
                }

                // Uncompressed-HDR path: expand RGBA half -> packed float32 for writeFloatHdrMips.
                if (target == basist::transcoder_texture_format::cTFRGBA_HALF)
                {
                    const size_t components = static_cast<size_t>(lw) * lh * 4;
                    std::vector<unsigned char> floatBytes(components * sizeof(float));
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(mip.data.data());
                    float* dst = reinterpret_cast<float*>(floatBytes.data());
                    for (size_t i = 0; i < components; ++i)
                        dst[i] = halfToFloat(src[i]);
                    mip.data = std::move(floatBytes);
                    mip.dataSize = static_cast<uint32_t>(mip.data.size());
                }

                result.levels.push_back(std::move(mip));
            }

            if (wantBc)
            {
                result.payload = DecodedTexture::Payload::BcBlocks;
                result.blockFormat = isHdr ? resource::TextureCompressionFormat::BC6H
                                           : resource::TextureCompressionFormat::BC7;
            }
            else
            {
                result.payload = isHdr ? DecodedTexture::Payload::Float
                                       : DecodedTexture::Payload::Rgba8;
            }
            result.ok = true;
            return result;
        }

        // ---------------------------- KTX1 (legacy, bcdec/passthrough/raw) ----------------------------

        // GL enums we recognize (subset). sRGB variants map to the same decode as
        // their linear counterparts — .vfImage stores no color space.
        enum : uint32_t
        {
            GL_RGB                                 = 0x1907,
            GL_RGBA                                = 0x1908,
            GL_BGRA                                = 0x80E1,
            GL_UNSIGNED_BYTE                       = 0x1401,
            GL_HALF_FLOAT                          = 0x140B,
            GL_FLOAT                               = 0x1406,

            GL_RGB8                                = 0x8051,
            GL_RGBA8                               = 0x8058,
            GL_SRGB8                               = 0x8C41,
            GL_SRGB8_ALPHA8                        = 0x8C43,
            GL_RGBA16F                             = 0x881A,
            GL_RGBA32F                             = 0x8814,

            GL_COMPRESSED_RGB_S3TC_DXT1            = 0x83F0,
            GL_COMPRESSED_RGBA_S3TC_DXT1           = 0x83F1,
            GL_COMPRESSED_RGBA_S3TC_DXT3           = 0x83F2,
            GL_COMPRESSED_RGBA_S3TC_DXT5           = 0x83F3,
            GL_COMPRESSED_SRGB_S3TC_DXT1           = 0x8C4C,
            GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1     = 0x8C4D,
            GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3     = 0x8C4E,
            GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5     = 0x8C4F,
            GL_COMPRESSED_RED_RGTC1                = 0x8DBB,
            GL_COMPRESSED_SIGNED_RED_RGTC1         = 0x8DBC,
            GL_COMPRESSED_RG_RGTC2                 = 0x8DBD,
            GL_COMPRESSED_SIGNED_RG_RGTC2          = 0x8DBE,
            GL_COMPRESSED_RGBA_BPTC_UNORM          = 0x8E8C,
            GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM    = 0x8E8D,
            GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT    = 0x8E8E,
            GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT  = 0x8E8F,
        };

        enum class Ktx1Mode { Unsupported, PassBC7, PassBC6H, DecodeBC, Rgba8, Bgra8, Rgb8, Float16, Float32 };

        Ktx1Mode classifyKtx1(uint32_t glType, uint32_t glFormat, uint32_t glInternalFormat, BcKind& bcOut)
        {
            if (glType == 0) // compressed
            {
                switch (glInternalFormat)
                {
                case GL_COMPRESSED_RGB_S3TC_DXT1:
                case GL_COMPRESSED_RGBA_S3TC_DXT1:
                case GL_COMPRESSED_SRGB_S3TC_DXT1:
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1: bcOut = BcKind::BC1; return Ktx1Mode::DecodeBC;
                case GL_COMPRESSED_RGBA_S3TC_DXT3:
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3: bcOut = BcKind::BC2; return Ktx1Mode::DecodeBC;
                case GL_COMPRESSED_RGBA_S3TC_DXT5:
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5: bcOut = BcKind::BC3; return Ktx1Mode::DecodeBC;
                case GL_COMPRESSED_RED_RGTC1:
                case GL_COMPRESSED_SIGNED_RED_RGTC1: bcOut = BcKind::BC4_UNORM; return Ktx1Mode::DecodeBC;
                case GL_COMPRESSED_RG_RGTC2:
                case GL_COMPRESSED_SIGNED_RG_RGTC2: bcOut = BcKind::BC5_UNORM; return Ktx1Mode::DecodeBC;
                case GL_COMPRESSED_RGBA_BPTC_UNORM:
                case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM: return Ktx1Mode::PassBC7;
                case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
                case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT: return Ktx1Mode::PassBC6H;
                default: return Ktx1Mode::Unsupported;
                }
            }

            // Uncompressed.
            if (glType == GL_UNSIGNED_BYTE)
            {
                if (glFormat == GL_BGRA) return Ktx1Mode::Bgra8;
                if (glFormat == GL_RGBA || glInternalFormat == GL_RGBA8 || glInternalFormat == GL_SRGB8_ALPHA8)
                    return Ktx1Mode::Rgba8;
                if (glFormat == GL_RGB || glInternalFormat == GL_RGB8 || glInternalFormat == GL_SRGB8)
                    return Ktx1Mode::Rgb8;
            }
            else if (glType == GL_HALF_FLOAT && (glFormat == GL_RGBA || glInternalFormat == GL_RGBA16F))
            {
                return Ktx1Mode::Float16;
            }
            else if (glType == GL_FLOAT && (glFormat == GL_RGBA || glInternalFormat == GL_RGBA32F))
            {
                return Ktx1Mode::Float32;
            }
            return Ktx1Mode::Unsupported;
        }

        // Build one output mip from a raw uncompressed level, honoring the 4-byte
        // row alignment KTX1 mandates (GL UNPACK_ALIGNMENT = 4).
        resource::MipLevelData buildUncompressedMip(const unsigned char* src, uint32_t lw, uint32_t lh,
                                                    Ktx1Mode mode)
        {
            resource::MipLevelData mip;
            mip.width = lw;
            mip.height = lh;

            auto align4 = [](uint32_t v) { return (v + 3u) & ~3u; };

            if (mode == Ktx1Mode::Rgba8 || mode == Ktx1Mode::Bgra8)
            {
                const uint32_t rowStride = align4(lw * 4);
                mip.data.resize(static_cast<size_t>(lw) * lh * 4);
                for (uint32_t y = 0; y < lh; ++y)
                {
                    const unsigned char* s = src + static_cast<size_t>(y) * rowStride;
                    unsigned char* d = mip.data.data() + static_cast<size_t>(y) * lw * 4;
                    for (uint32_t x = 0; x < lw; ++x)
                    {
                        if (mode == Ktx1Mode::Bgra8)
                        {
                            d[x * 4 + 0] = s[x * 4 + 2];
                            d[x * 4 + 1] = s[x * 4 + 1];
                            d[x * 4 + 2] = s[x * 4 + 0];
                            d[x * 4 + 3] = s[x * 4 + 3];
                        }
                        else
                        {
                            std::memcpy(d + x * 4, s + x * 4, 4);
                        }
                    }
                }
            }
            else // Rgb8 -> RGBA8 (A=255)
            {
                const uint32_t rowStride = align4(lw * 3);
                mip.data.resize(static_cast<size_t>(lw) * lh * 4);
                for (uint32_t y = 0; y < lh; ++y)
                {
                    const unsigned char* s = src + static_cast<size_t>(y) * rowStride;
                    unsigned char* d = mip.data.data() + static_cast<size_t>(y) * lw * 4;
                    for (uint32_t x = 0; x < lw; ++x)
                    {
                        d[x * 4 + 0] = s[x * 3 + 0];
                        d[x * 4 + 1] = s[x * 3 + 1];
                        d[x * 4 + 2] = s[x * 3 + 2];
                        d[x * 4 + 3] = 255;
                    }
                }
            }
            return mip;
        }

        // Bytes one mip level MUST occupy on disk for the given mode/dimensions, so a level whose
        // declared imageSize is short can be rejected before any branch reads past it. Rows are
        // padded to 4 bytes (KTX1 UNPACK_ALIGNMENT=4) — buildUncompressedMip strides by exactly
        // align4(lw * bpp), so the check has to use the padded stride, not lw * lh * bpp.
        // Returns 0 for the passthrough modes: those consume exactly imageSize, which the caller
        // has already bounds-checked against the file, so there is nothing extra to validate.
        uint64_t ktx1LevelBytes(Ktx1Mode mode, BcKind bcKind, uint32_t lw, uint32_t lh)
        {
            auto align4 = [](uint64_t v) { return (v + 3u) & ~3ull; };
            const uint64_t w = lw;
            const uint64_t h = lh;
            switch (mode)
            {
            case Ktx1Mode::PassBC7:
            case Ktx1Mode::PassBC6H: return 0;
            case Ktx1Mode::DecodeBC: return bcLevelByteSize(lw, lh, bcKind);
            case Ktx1Mode::Rgba8:
            case Ktx1Mode::Bgra8:    return align4(w * 4) * h;
            case Ktx1Mode::Rgb8:     return align4(w * 3) * h;
            case Ktx1Mode::Float16:  return w * h * 4 * sizeof(uint16_t);
            case Ktx1Mode::Float32:  return w * h * 4 * sizeof(float);
            default:                 return 0;
            }
        }

        DecodedTexture decodeKtx1(const std::vector<uint8_t>& bytes)
        {
            DecodedTexture result;

            // 12-byte id + 13 uint32 header fields = 64 bytes.
            if (bytes.size() < 64)
            {
                vfLogError("KTX1: truncated header");
                return result;
            }

            const uint32_t endianField = readU32(&bytes[12], /*bigEndian=*/false);
            const bool bigEndian = (endianField == 0x01020304u);
            auto rd = [&](size_t off) { return readU32(&bytes[off], bigEndian); };

            const uint32_t glType = rd(16);
            const uint32_t glFormat = rd(24);
            const uint32_t glInternalFormat = rd(28);
            const uint32_t pixelWidth = rd(36);
            uint32_t pixelHeight = rd(40);
            const uint32_t pixelDepth = rd(44);
            const uint32_t numArrayElements = rd(48);
            const uint32_t numFaces = rd(52);
            uint32_t numMips = rd(56);
            const uint32_t kvdBytes = rd(60);

            if (pixelWidth == 0)
            {
                vfLogError("KTX1: zero width");
                return result;
            }
            if (pixelHeight == 0) pixelHeight = 1;
            if (pixelDepth > 1 || numArrayElements > 1 || numFaces > 1)
            {
                vfLogWarning("KTX1: only 2D non-array non-cube textures are supported; skipping");
                return result;
            }
            if (numMips == 0) numMips = 1; // 0 = "generate at load"; one base level is stored.

            // Same defensive bounds the KTX2 path applies: reject implausible dimensions/levels so a
            // malformed header that still parses can't drive a huge reserve() before the per-level
            // truncation checks ever run.
            if (pixelWidth > 65536 || pixelHeight > 65536 || numMips > 24)
            {
                vfLogWarning("KTX1: implausible dimensions {}x{} ({} levels); skipping",
                             pixelWidth, pixelHeight, numMips);
                return result;
            }

            BcKind bcKind = BcKind::BC1;
            const Ktx1Mode mode = classifyKtx1(glType, glFormat, glInternalFormat, bcKind);
            if (mode == Ktx1Mode::Unsupported)
            {
                vfLogWarning("KTX1: unsupported format (glType=0x{:X}, glFormat=0x{:X}, glInternalFormat=0x{:X}); "
                             "skipping", glType, glFormat, glInternalFormat);
                return result;
            }

            result.width = pixelWidth;
            result.height = pixelHeight;

            switch (mode)
            {
            case Ktx1Mode::PassBC7: result.payload = DecodedTexture::Payload::BcBlocks;
                                    result.blockFormat = resource::TextureCompressionFormat::BC7; break;
            case Ktx1Mode::PassBC6H: result.payload = DecodedTexture::Payload::BcBlocks;
                                     result.blockFormat = resource::TextureCompressionFormat::BC6H;
                                     result.isHdr = true; break;
            case Ktx1Mode::Float16:
            case Ktx1Mode::Float32: result.payload = DecodedTexture::Payload::Float; result.isHdr = true; break;
            default: result.payload = DecodedTexture::Payload::Rgba8; break;
            }

            size_t offset = 64 + kvdBytes;
            result.levels.reserve(numMips);

            for (uint32_t level = 0; level < numMips; ++level)
            {
                if (offset + 4 > bytes.size())
                {
                    vfLogError("KTX1: truncated level index (level {})", level);
                    return DecodedTexture{};
                }
                const uint32_t imageSize = rd(offset);
                offset += 4;
                if (offset + imageSize > bytes.size())
                {
                    vfLogError("KTX1: truncated level data (level {})", level);
                    return DecodedTexture{};
                }
                const unsigned char* levelData = &bytes[offset];
                const uint32_t lw = std::max(1u, pixelWidth >> level);
                const uint32_t lh = std::max(1u, pixelHeight >> level);

                // The guard above only proves imageSize bytes are present. Every non-passthrough
                // branch below reads a size derived from lw/lh instead, so a level that declares a
                // smaller imageSize than its dimensions imply would read past the end of `bytes`.
                if (const uint64_t need = ktx1LevelBytes(mode, bcKind, lw, lh); imageSize < need)
                {
                    vfLogError("KTX1: level {} truncated ({}x{} needs {} bytes, imageSize is {})",
                               level, lw, lh, need, imageSize);
                    return DecodedTexture{};
                }

                resource::MipLevelData mip;
                switch (mode)
                {
                case Ktx1Mode::PassBC7:
                case Ktx1Mode::PassBC6H:
                    mip.width = lw;
                    mip.height = lh;
                    mip.dataSize = imageSize;
                    mip.data.assign(levelData, levelData + imageSize);
                    break;

                case Ktx1Mode::DecodeBC:
                {
                    // Size already validated by the ktx1LevelBytes check above.
                    auto rgba = decodeBcToRgba8(levelData, lw, lh, bcKind);
                    if (rgba.empty())
                    {
                        vfLogError("KTX1: BC decode failed (level {})", level);
                        return DecodedTexture{};
                    }
                    mip.width = lw;
                    mip.height = lh;
                    mip.data = std::move(rgba);
                    break;
                }

                case Ktx1Mode::Rgba8:
                case Ktx1Mode::Bgra8:
                case Ktx1Mode::Rgb8:
                    mip = buildUncompressedMip(levelData, lw, lh, mode);
                    break;

                case Ktx1Mode::Float16:
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

                case Ktx1Mode::Float32:
                    mip.width = lw;
                    mip.height = lh;
                    mip.data.assign(levelData, levelData + static_cast<size_t>(lw) * lh * 4 * sizeof(float));
                    mip.dataSize = static_cast<uint32_t>(mip.data.size());
                    break;

                default:
                    return DecodedTexture{};
                }

                result.levels.push_back(std::move(mip));

                offset += imageSize;
                offset += (3 - ((imageSize + 3) & 3)); // mipPadding to 4-byte boundary
            }

            result.ok = true;
            return result;
        }
    }

    TextureImportResult Texture::loadKtxFile(const importConfig::ImportFiles& file, std::string_view fileName,
                                             std::string_view location, TextureProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        std::vector<uint8_t> bytes;
        if (!readWholeFile(file.path, bytes) || bytes.size() < 12)
        {
            vfLogError("KTX: cannot read '{}'", file.path);
            return TextureImportResult::Failed;
        }

        // Bound concurrent heavy decodes and account the scratch memory (shared cap
        // with the stb/HDR paths). ~6x the file size covers transcode + mip pyramid.
        ImportDecodeLock decodeLock;
        memory::ScopedCpuMemory decodeGuard(texDecodeCategory(), static_cast<uint64_t>(bytes.size()) * 6u);

        DecodedTexture decoded;
        if (std::memcmp(bytes.data(), kKtx2Id, 12) == 0)
            decoded = decodeKtx2(bytes, file.config);
        else if (std::memcmp(bytes.data(), kKtx1Id, 12) == 0)
            decoded = decodeKtx1(bytes);
        else
        {
            vfLogError("KTX: '{}' is not a KTX or KTX2 file", file.path);
            return TextureImportResult::Failed;
        }

        if (progressCallback) progressCallback(0.6f);
        const TextureImportResult res = writeDecodedTexture(std::move(decoded), fileName, location, file.config);
        if (progressCallback) progressCallback(1.0f);
        return res;
    }
}
