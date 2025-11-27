#pragma once
#include <cstdint>
#include <bit>
#include <fstream>

namespace resource::endian
{
    // Force little-endian for all binary formats (most common, used by x86/x64)
    
    // Byte swapping functions
    constexpr uint16_t byteswap16(uint16_t value) noexcept
    {
        return (value << 8) | (value >> 8);
    }
    
    constexpr uint32_t byteswap32(uint32_t value) noexcept
    {
        return ((value << 24) & 0xFF000000) |
               ((value << 8)  & 0x00FF0000) |
               ((value >> 8)  & 0x0000FF00) |
               ((value >> 24) & 0x000000FF);
    }
    
    constexpr uint64_t byteswap64(uint64_t value) noexcept
    {
        return ((value << 56) & 0xFF00000000000000ULL) |
               ((value << 40) & 0x00FF000000000000ULL) |
               ((value << 24) & 0x0000FF0000000000ULL) |
               ((value << 8)  & 0x000000FF00000000ULL) |
               ((value >> 8)  & 0x00000000FF000000ULL) |
               ((value >> 24) & 0x0000000000FF0000ULL) |
               ((value >> 40) & 0x000000000000FF00ULL) |
               ((value >> 56) & 0x00000000000000FFULL);
    }

    // Convert to little-endian (for writing)
    template<typename T>
    constexpr T toLittleEndian(T value) noexcept
    {
        if constexpr (std::endian::native == std::endian::little)
        {
            return value; // Already little-endian, no conversion needed
        }
        else
        {
            // Big-endian system, need to swap bytes
            if constexpr (sizeof(T) == 1)
                return value;
            else if constexpr (sizeof(T) == 2)
                return static_cast<T>(byteswap16(static_cast<uint16_t>(value)));
            else if constexpr (sizeof(T) == 4)
                return static_cast<T>(byteswap32(static_cast<uint32_t>(value)));
            else if constexpr (sizeof(T) == 8)
                return static_cast<T>(byteswap64(static_cast<uint64_t>(value)));
            else
                static_assert(sizeof(T) <= 8, "Unsupported type size for endian conversion");
        }
    }

    // Convert from little-endian (for reading)  
    template<typename T>
    constexpr T fromLittleEndian(T value) noexcept
    {
        // Same implementation as toLittleEndian since conversion is symmetric
        return toLittleEndian(value);
    }

    // Float conversion (reinterpret as uint32_t, swap, reinterpret back)
    inline float toLittleEndian(float value) noexcept
    {
        uint32_t intValue = std::bit_cast<uint32_t>(value);
        uint32_t swappedValue = toLittleEndian(intValue);
        return std::bit_cast<float>(swappedValue);
    }
    
    inline float fromLittleEndian(float value) noexcept
    {
        return toLittleEndian(value); // Symmetric operation
    }

    // Endian-safe file I/O functions
    template<typename T>
    inline void writeLE(std::ofstream& file, T value)
    {
        T leValue = toLittleEndian(value);
        file.write(reinterpret_cast<const char*>(&leValue), sizeof(T));
    }

    template<typename T> 
    inline T readLE(std::ifstream& file)
    {
        T value;
        file.read(reinterpret_cast<char*>(&value), sizeof(T));
        return fromLittleEndian(value);
    }

    // Vector writing/reading for bulk data
    template<typename T>
    inline void writeVectorLE(std::ofstream& file, const std::vector<T>& vec)
    {
        if constexpr (std::endian::native == std::endian::little)
        {
            // Fast path: direct write on little-endian systems
            file.write(reinterpret_cast<const char*>(vec.data()), vec.size() * sizeof(T));
        }
        else
        {
            // Slow path: convert each element on big-endian systems
            for (const auto& value : vec)
            {
                writeLE(file, value);
            }
        }
    }

    template<typename T>
    inline void readVectorLE(std::ifstream& file, std::vector<T>& vec, size_t count)
    {
        vec.resize(count);
        
        if constexpr (std::endian::native == std::endian::little)
        {
            // Fast path: direct read on little-endian systems
            file.read(reinterpret_cast<char*>(vec.data()), count * sizeof(T));
        }
        else
        {
            // Slow path: convert each element on big-endian systems  
            for (size_t i = 0; i < count; ++i)
            {
                vec[i] = readLE<T>(file);
            }
        }
    }
}