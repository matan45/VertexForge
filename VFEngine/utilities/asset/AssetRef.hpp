#pragma once
#include "AssetGUID.hpp"
#include <string>

namespace asset
{
    class AssetRef
    {
    public:
        AssetRef() = default;
        explicit AssetRef(const AssetGUID& guid) : guid(guid) {}

        static AssetRef fromGUID(const AssetGUID& guid) { return AssetRef(guid); }
        static AssetRef fromPath(const std::string& path);
        static AssetRef fromHexString(const std::string& hex);
        static AssetRef invalid() { return AssetRef(); }

        bool isValid() const { return guid.isValid(); }
        AssetGUID getGUID() const { return guid; }

        // Resolve GUID to current file path via AssetDatabase
        std::string resolve() const;

        // Serialize to/from hex string for JSON
        std::string toHexString() const { return guid.toString(); }

        bool operator==(const AssetRef& other) const { return guid == other.guid; }
        bool operator!=(const AssetRef& other) const { return guid != other.guid; }
        bool operator<(const AssetRef& other) const { return guid < other.guid; }

        struct Hash
        {
            size_t operator()(const AssetRef& ref) const
            {
                return AssetGUID::Hash{}(ref.guid);
            }
        };

    private:
        AssetGUID guid;
    };
}
