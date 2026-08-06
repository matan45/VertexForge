#pragma once
#include "TerrainExport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// VK-1614 terrain surface mask — the world-anchored, editor-paintable wetness/snow map.
//
// Stored as a plain uncompressed `.vfImage` (the project rule for terrain image assets), which makes
// it authorable by anything that can write one: the paint brush today, the weather system later, or
// an external tool.
//
// WHY THIS EXISTS RATHER THAN HeightmapLoader:
// HeightmapLoader parses the identical header but then collapses the pixels to a single grayscale
// luminance channel, and applies a 16-bit heightmap heuristic on the way. This mask needs two
// INDEPENDENT channels, so it needs a reader that keeps them. HeightmapLoader is left untouched on
// purpose — it feeds sculpt stamps, terrain creation and world streaming, and is mirrored into
// sdk/include.
//
// WHY IT LIVES IN THE TERRAIN DLL rather than reusing procedural::VFImageWriter:
// that writer lives in ProceduralGen, which ships only to bin/Editor (premake5.lua). Terrain ships
// to Editor AND Runtime, so linking it would drag an editor-only tool DLL into the Runtime closure,
// straight through the module dependency graph in CLAUDE.md — and it would foreclose the ticket's own
// "the weather system may write it later". The header is 45 bytes and resource::endian is header-only
// inline, so keeping both ends here costs no new link and no premake edit.
// (The repo already carries four copies of this writer. Consolidating them into Utilities is filed as
// a follow-up; when it lands, this collapses onto it.)

namespace terrain
{
    // Channel assignment inside the RGBA8 payload. B is reserved for a future third signal.
    //
    // A IS ALWAYS 255, and that is not cosmetic: HeightmapLoader treats ANY non-255 alpha in an
    // uncompressed .vfImage as "this is a 16-bit heightmap, RGB is the high byte and A the low byte".
    // A mask written with a varying alpha would therefore decode as garbage the moment someone
    // pointed a stamp brush at it.
    inline constexpr uint32_t SURFACE_MASK_CHANNELS = 4;
    inline constexpr uint32_t SURFACE_MASK_WETNESS_CHANNEL = 0; // R
    inline constexpr uint32_t SURFACE_MASK_SNOW_CHANNEL = 1;    // G

    inline constexpr uint32_t SURFACE_MASK_DEFAULT_RESOLUTION = 1024;
    inline constexpr uint32_t SURFACE_MASK_MIN_RESOLUTION = 64;
    // 8192^2 RGBA8 is 256 MB of CPU bytes and the same again in VRAM. Well past useful for a
    // low-frequency accumulation mask, so it is the refusal point rather than a target.
    inline constexpr uint32_t SURFACE_MASK_MAX_RESOLUTION = 8192;

#pragma warning(push)
#pragma warning(disable: 4251)
    // Row-major RGBA8, always exactly width * height * 4 bytes when valid.
    struct VF_TERRAIN_API TerrainSurfaceMaskData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgba;

        [[nodiscard]] bool isValid() const
        {
            return width > 0 && height > 0
                && rgba.size() == static_cast<size_t>(width) * height * SURFACE_MASK_CHANNELS;
        }

        [[nodiscard]] size_t texelCount() const
        {
            return static_cast<size_t>(width) * height;
        }

        // Normalized [0, 1] read of one channel. Out-of-range coordinates return 0 — the neutral
        // "no local contribution" value, matching what the shader does outside the mask rect.
        [[nodiscard]] float getChannel(uint32_t x, uint32_t z, uint32_t channel) const;
        void setChannel(uint32_t x, uint32_t z, uint32_t channel, float value);

        // Bilinear read in UV space, mirroring the GPU sampler (linear filter, clamp-to-edge) so a
        // CPU query and the shader agree. Used by the brush to read back what it is about to modify.
        [[nodiscard]] float sample(float u, float v, uint32_t channel) const;
    };
#pragma warning(pop)

    class VF_TERRAIN_API TerrainSurfaceMaskAsset
    {
    public:
        // Reads an uncompressed (BGRA) or BC7-compressed `.vfImage` and returns its mip 0 as RGBA8.
        // Returns nullptr on any failure; never returns a partially-populated buffer.
        static std::shared_ptr<TerrainSurfaceMaskData> load(const std::string& filePath);

        // Writes an uncompressed single-mip `.vfImage`. Alpha is forced to 255 on the way out
        // regardless of what the caller's buffer holds — see the note on SURFACE_MASK_CHANNELS.
        static bool save(const std::string& filePath, const TerrainSurfaceMaskData& data);

        // All-zero mask (no local wetness, no local snow) at the given square resolution, clamped to
        // [SURFACE_MASK_MIN_RESOLUTION, SURFACE_MASK_MAX_RESOLUTION] and rounded to a multiple of 4
        // so a later BC7 pass has whole blocks to work with.
        static std::shared_ptr<TerrainSurfaceMaskData> createEmpty(uint32_t resolution);
    };
}
