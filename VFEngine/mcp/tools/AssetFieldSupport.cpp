#include "AssetFieldSupport.hpp"
#include "PathSandbox.hpp"
#include "TerrainToolSupport.hpp"
#include "../protocol/ArgReader.hpp"

#include <optional>
#include <system_error>

namespace mcp::tools
{
    namespace
    {
        namespace fs = std::filesystem;

        bool hasExtension(const fs::path& path, std::string_view extension)
        {
            return path.has_filename() && detail::componentEquals(path.extension(), pathFromUtf8(extension));
        }
    }

    fs::path agentAssetPath(const std::string& text)
    {
        fs::path path = pathFromUtf8(text);
        if (path.is_relative())
        {
            if (const std::optional<fs::path> root = tryTerrainProjectRoot())
            {
                path = *root / path;
            }
        }
        return path.lexically_normal();
    }

    asset::AssetRef assetFieldFromJson(const nlohmann::json& value, const char* key, std::string_view extension,
                                       AssetScope scope)
    {
        const std::string type(extension);
        if (value.is_null())
        {
            return asset::AssetRef::invalid();
        }
        if (!value.is_string())
        {
            throw ArgError(std::string("field '") + key + "' must be a " + type + " path string");
        }
        const std::string text = value.get<std::string>();
        if (text.empty())
        {
            return asset::AssetRef::invalid();
        }

        static constexpr std::string_view guidPrefix = "guid:";
        if (text.starts_with(guidPrefix))
        {
            asset::AssetRef ref = asset::AssetRef::fromHexString(text.substr(guidPrefix.size()));
            if (!ref.isValid())
            {
                throw ArgError(std::string("field '") + key + "': '" + text + "' is not a valid asset GUID");
            }
            // A GUID whose file cannot be found stays assignable, as component_get reports it.
            const std::string& path = ref.resolve();
            if (!path.empty() && !hasExtension(fs::path(path), extension))
            {
                throw ArgError(std::string("field '") + key + "': '" + text + "' is not a " + type + " asset");
            }
            if (!path.empty() && scope == AssetScope::InsideProject)
            {
                std::string error;
                if (!resolveInside(terrainProjectRoot(), pathToUtf8(fs::path(path)), error).has_value())
                {
                    throw ArgError(std::string("field '") + key + "': '" + text + "' names '" +
                                   genericPathToUtf8(fs::path(path)) + "', which lies outside the project");
                }
            }
            return ref;
        }

        fs::path file;
        if (scope == AssetScope::InsideProject)
        {
            // The agent's spelling first (the clearer error); resolveInside checks the resolved file again,
            // which a link could have renamed.
            if (!hasExtension(pathFromUtf8(text), extension))
            {
                throw ArgError(std::string("field '") + key + "' must be a " + type + " file (got '" + text + "')");
            }
            std::string error;
            const std::optional<fs::path> inside = resolveInside(terrainProjectRoot(), text, error, extension);
            if (!inside.has_value())
            {
                throw ArgError(std::string("field '") + key + "': " + error);
            }
            file = *inside;
        }
        else
        {
            file = agentAssetPath(text);
            if (!hasExtension(file, extension))
            {
                throw ArgError(std::string("field '") + key + "' must be a " + type + " file (got '" + text + "')");
            }
        }
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            throw ArgError(std::string("field '") + key + "': '" + text + "' does not exist");
        }
        asset::AssetRef ref = asset::AssetRef::fromPath(file.string());
        if (!ref.isValid())
        {
            throw ArgError(std::string("field '") + key + "': '" + text + "' is not an engine asset");
        }
        return ref;
    }

    nlohmann::json assetFieldJson(const asset::AssetRef& ref, AssetScope scope)
    {
        if (!ref.isValid())
        {
            return "";
        }
        const std::string& path = ref.resolve();
        if (path.empty())
        {
            return "guid:" + ref.toHexString();
        }
        return scope == AssetScope::InsideProject ? projectRelativeUtf8(tryTerrainProjectRoot(), fs::path(path))
                                                  : genericPathToUtf8(fs::path(path));
    }
}
