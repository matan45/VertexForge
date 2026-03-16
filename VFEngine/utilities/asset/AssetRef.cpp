#include "AssetRef.hpp"
#include "AssetDatabase.hpp"

namespace asset
{
    AssetRef AssetRef::fromPath(const std::string& path)
    {
        if (path.empty()) return invalid();

        auto guidOpt = AssetDatabase::instance().getGUID(path);
        if (guidOpt)
        {
            return AssetRef(*guidOpt);
        }
        return invalid();
    }

    AssetRef AssetRef::fromHexString(const std::string& hex)
    {
        if (hex.empty()) return invalid();
        return AssetRef(AssetGUID::fromString(hex));
    }

    std::string AssetRef::resolve() const
    {
        if (!guid.isValid()) return {};

        auto pathOpt = AssetDatabase::instance().getPath(guid);
        if (pathOpt)
        {
            return *pathOpt;
        }
        return {};
    }
}
