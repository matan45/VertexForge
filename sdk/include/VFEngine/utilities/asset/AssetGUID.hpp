#pragma once
#include "../uuid/UUID.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <sstream>
#include <iomanip>

namespace asset
{
    class AssetGUID
    {
    public:
        AssetGUID() : id(uuid::UUID::invalid()) {}

        static AssetGUID generate()
        {
            AssetGUID guid;
            guid.id = uuid::UUID();
            return guid;
        }

        static AssetGUID invalid() { return AssetGUID(); }

        static AssetGUID fromValue(uint64_t value)
        {
            AssetGUID guid;
            guid.id = uuid::UUID(value);
            return guid;
        }

        // True only for the exact 16-hex-digit form toString() emits.
        // fromString stays lenient (stoull base 16), so callers extracting
        // GUIDs from arbitrary strings must gate on this first.
        static bool isStrictHex16(const std::string& str)
        {
            if (str.size() != 16) return false;
            return std::all_of(str.begin(), str.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            });
        }

        static AssetGUID fromString(const std::string& hexStr)
        {
            if (hexStr.empty()) return invalid();

            try
            {
                uint64_t value = std::stoull(hexStr, nullptr, 16);
                return fromValue(value);
            }
            catch (const std::exception&)
            {
                return invalid();
            }
        }

        std::string toString() const
        {
            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(16) << id.getValue();
            return ss.str();
        }

        bool isValid() const { return id.isValid(); }

        uint64_t getValue() const { return id.getValue(); }

        bool operator==(const AssetGUID& other) const { return id == other.id; }
        bool operator!=(const AssetGUID& other) const { return id != other.id; }
        bool operator<(const AssetGUID& other) const { return id < other.id; }

        struct Hash
        {
            size_t operator()(const AssetGUID& guid) const
            {
                return std::hash<uint64_t>{}(guid.id.getValue());
            }
        };

    private:
        uuid::UUID id;
    };
}
