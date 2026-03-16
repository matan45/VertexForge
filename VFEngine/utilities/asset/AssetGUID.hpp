#pragma once
#include "../uuid/UUID.hpp"
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
