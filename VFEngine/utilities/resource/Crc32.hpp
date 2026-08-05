#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace resource
{
    // CRC-32/ISO-HDLC — reflected, polynomial 0xEDB88320, initial 0xFFFFFFFF, final XOR
    // 0xFFFFFFFF. The same variant zlib, PNG and gzip use.
    //
    // Header-only and `inline` on purpose: it has no export macro, so a SharedLib subsystem
    // (Terrain) and Tests.exe each get their own copy with no DLL boundary to cross and no ABI to
    // keep in step. Same consumption model as EndianUtils.hpp.
    //
    // This is deliberately NOT archive::hashBytes (FNV-1a 64). The two answer different questions:
    // FNV-1a identifies a *generation* of a whole file, CRC32 detects *corruption* of one indexed
    // block, and the VFTL format specifies CRC32 for the latter.
    namespace detail
    {
        inline constexpr std::array<uint32_t, 256> makeCrc32Table() noexcept
        {
            std::array<uint32_t, 256> table{};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t value = i;
                for (int bit = 0; bit < 8; ++bit)
                    value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
                table[i] = value;
            }
            return table;
        }

        inline constexpr std::array<uint32_t, 256> CRC32_TABLE = makeCrc32Table();
    }

    // Streaming form: seed with crc32Init(), fold with crc32Update(), close with crc32Finish().
    // Splitting it lets a writer checksum a block it is emitting piecewise without buffering it.
    [[nodiscard]] inline constexpr uint32_t crc32Init() noexcept { return 0xFFFFFFFFu; }

    [[nodiscard]] inline constexpr uint32_t crc32Finish(uint32_t state) noexcept
    {
        return state ^ 0xFFFFFFFFu;
    }

    [[nodiscard]] inline uint32_t crc32Update(uint32_t state, const uint8_t* data,
                                              size_t size) noexcept
    {
        for (size_t i = 0; i < size; ++i)
            state = detail::CRC32_TABLE[(state ^ data[i]) & 0xFFu] ^ (state >> 8);
        return state;
    }

    // Text overload, which unlike the pointer one is usable in a constant expression: a
    // reinterpret_cast is not allowed there, so the self-test below could not exist without it.
    [[nodiscard]] inline constexpr uint32_t crc32Update(uint32_t state,
                                                        std::string_view text) noexcept
    {
        for (char c : text)
            state = detail::CRC32_TABLE[(state ^ static_cast<uint8_t>(c)) & 0xFFu] ^ (state >> 8);
        return state;
    }

    [[nodiscard]] inline uint32_t crc32(const uint8_t* data, size_t size) noexcept
    {
        return crc32Finish(crc32Update(crc32Init(), data, size));
    }

    [[nodiscard]] inline constexpr uint32_t crc32(std::string_view text) noexcept
    {
        return crc32Finish(crc32Update(crc32Init(), text));
    }

    // The published check value for this CRC variant. A transcription slip in the polynomial, the
    // reflection or either XOR would move it, and this catches that at compile time rather than as
    // a mysteriously unreadable sidecar.
    static_assert(crc32(std::string_view{"123456789"}) == 0xCBF43926u,
                  "CRC-32/ISO-HDLC check value mismatch — the implementation is wrong");
}
