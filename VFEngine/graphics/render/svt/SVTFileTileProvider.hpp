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
        std::unique_ptr<SVTFileReader> albedoReader_;
        std::unique_ptr<SVTFileReader> normalReader_;
        std::unique_ptr<SVTFileReader> ormReader_;
        mutable std::mutex readerMutex_;

    public:
        SVTFileTileProvider() = default;
        ~SVTFileTileProvider() override = default;

        bool openFiles(const std::string& albedoPath,
                       const std::string& normalPath,
                       const std::string& ormPath);

        void close();

        SVTTileData generateTile(const VirtualTileCoord& coord) override;

        bool hasAlbedo() const { return albedoReader_ && albedoReader_->isValid(); }
        bool hasNormal() const { return normalReader_ && normalReader_->isValid(); }
        bool hasORM() const { return ormReader_ && ormReader_->isValid(); }
    };
}
