#pragma once

#include "SVTStreamManager.hpp"
#include "SVTFileFormat.hpp"
#include <string>
#include <memory>
#include <mutex>

namespace render::svt
{
    // Tile provider that reads pre-baked tiles from .vfSVT files.
    // Used for scene materials (vfMaterial/vfInstancedMaterial) where textures
    // are tiled at import time.
    //
    // Each material texture channel (albedo, normal, ORM) has its own .vfSVT file.
    class SVTFileTileProvider : public SVTTileProvider
    {
    private:
        std::unique_ptr<SVTFileReader> albedoReader;
        std::unique_ptr<SVTFileReader> normalReader;
        std::unique_ptr<SVTFileReader> ormReader;
        mutable std::mutex readerMutex;

    public:
        SVTFileTileProvider() = default;
        ~SVTFileTileProvider() override = default;

        bool openFiles(const std::string& albedoPath,
                       const std::string& normalPath,
                       const std::string& ormPath);

        void close();

        SVTTileData generateTile(const VirtualTileCoord& coord) override;

        bool hasAlbedo() const { return albedoReader && albedoReader->isValid(); }
        bool hasNormal() const { return normalReader && normalReader->isValid(); }
        bool hasORM() const { return ormReader && ormReader->isValid(); }
    };
}
