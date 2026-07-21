#pragma once
#include <cstdint>
#include <vector>
#include "resource/Types.hpp"

// VK-1642: shared block-decode helpers used by both KtxTextureImport.cpp and
// DdsTextureImport.cpp. Pure functions (no Texture dependency); the write-side
// assembly that reuses Texture's private writers lives as Texture methods
// (declared in Texture.hpp, defined in BlockTextureAssembly.cpp).
//
// Only the formats that have NO representable .vfImage/.vfHdr enum code
// (BC1/2/3/4/5) are decoded here; BC7 and BC6H are passed through verbatim by
// the callers, so they are intentionally NOT handled by decodeBcToRgba8.
namespace types
{
    // The result of parsing a KTX/DDS container into a form the Texture writers
    // accept. `levels` holds one entry per mip (largest first): BC7/BC6H blocks
    // for BcBlocks, tightly-packed RGBA8 for Rgba8, or packed float32 RGBA for
    // Float. The caller maps payload -> writeBlockMips / writeRgba8Mips /
    // writeFloatHdrMips.
    struct DecodedTexture
    {
        enum class Payload { BcBlocks, Rgba8, Float };
        bool ok = false;
        bool isHdr = false;                        // true => write .vfHdr (HDR asset)
        Payload payload = Payload::Rgba8;
        resource::TextureCompressionFormat blockFormat =
            resource::TextureCompressionFormat::BC7; // used only when payload == BcBlocks
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<resource::MipLevelData> levels;
    };

    enum class BcKind
    {
        BC1,        // DXT1 (RGB, 1-bit alpha) -> RGBA8, 8-byte block
        BC2,        // DXT3 (RGBA, sharp alpha) -> RGBA8, 16-byte block
        BC3,        // DXT5 (RGBA, smooth alpha) -> RGBA8, 16-byte block
        BC4_UNORM,  // single channel (R) -> replicated to RGB, 8-byte block
        BC5_UNORM   // two channel (RG) -> B=0, 16-byte block
    };

    // Bytes per 4x4 block (8 for BC1/BC4, 16 for BC2/BC3/BC5).
    uint32_t bcBlockBytes(BcKind kind);

    // Bytes for one mip level of `kind` at width x height (padded to 4x4 blocks).
    uint32_t bcLevelByteSize(uint32_t width, uint32_t height, BcKind kind);

    // Decode one mip level of BC1/2/3/4/5 blocks (tightly packed, 4x4-block order)
    // into a tightly-packed RGBA8 buffer (width*height*4 bytes, top-left origin).
    // BC4 replicates its single channel to RGB (A=255); BC5 maps RG, B=0, A=255.
    // Returns empty on unsupported kind. The caller guarantees `blocks` holds at
    // least bcLevelByteSize(width,height,kind) bytes.
    std::vector<unsigned char> decodeBcToRgba8(const unsigned char* blocks,
                                               uint32_t width, uint32_t height, BcKind kind);

    // IEEE-754 half -> float. For uncompressed R16G16B16A16_FLOAT KTX2/DDS payloads.
    float halfToFloat(uint16_t h);
}
