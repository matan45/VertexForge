#include "VfsBridge.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "serialization/SerializationFileAccess.hpp"
#include "terrain/TerrainFileAccess.hpp"
#include <optional>
#include <string>

namespace services
{
    bool configureVfsBridges()
    {
        auto& vfs = resource::VirtualFileSystem::instance();
        auto* const vfsPtr = &vfs;
        const bool terrainConfigured = terrain::setTerrainFileAccess({
            [vfsPtr](const std::string& path) -> std::optional<terrain::TerrainFileLocation>
            {
                const auto location = vfsPtr->locate(path);
                if (!location) return std::nullopt;
                return terrain::TerrainFileLocation{
                    location->filePath, location->baseOffset, location->size};
            },
            [vfsPtr](const std::string& path) { return vfsPtr->readFile(path); },
            [vfsPtr](const std::string& path) { return vfsPtr->exists(path); },
            [vfsPtr] { return vfsPtr->isArchiveMode(); }});

        const bool serializationConfigured = serialization::setSerializationFileAccess({
            [vfsPtr](const std::string& path) { return vfsPtr->readFile(path); },
            [vfsPtr](const std::string& path) { return vfsPtr->exists(path); },
            [vfsPtr] { return vfsPtr->isArchiveMode(); }});
        return terrainConfigured && serializationConfigured;
    }
}
