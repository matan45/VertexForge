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

        // Resolve GUID to current file path via AssetDatabase.
        // Result is cached — first call does a DB lookup, subsequent calls return cached value.
        const std::string& resolve() const;

        // Force re-resolve on next resolve() call (e.g., after asset rename/move)
        void invalidateCache() const { cachedPath.clear(); cacheValid = false; }

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
        mutable std::string cachedPath;
        mutable bool cacheValid = false;
    };
}
