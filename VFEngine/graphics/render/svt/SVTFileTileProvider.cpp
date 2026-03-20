#include "SVTFileTileProvider.hpp"
#include "print/Log.hpp"

namespace render::svt
{
    bool SVTFileTileProvider::openFiles(const std::string& albedoPath,
                                         const std::string& normalPath,
                                         const std::string& ormPath)
    {
        std::lock_guard<std::mutex> lock(readerMutex_);

        if (!albedoPath.empty())
        {
            albedoReader_ = std::make_unique<SVTFileReader>();
            if (!albedoReader_->open(albedoPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open albedo SVT: {}", albedoPath);
                albedoReader_.reset();
            }
        }

        if (!normalPath.empty())
        {
            normalReader_ = std::make_unique<SVTFileReader>();
            if (!normalReader_->open(normalPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open normal SVT: {}", normalPath);
                normalReader_.reset();
            }
        }

        if (!ormPath.empty())
        {
            ormReader_ = std::make_unique<SVTFileReader>();
            if (!ormReader_->open(ormPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open ORM SVT: {}", ormPath);
                ormReader_.reset();
            }
        }

        return albedoReader_ || normalReader_ || ormReader_;
    }

    void SVTFileTileProvider::close()
    {
        std::lock_guard<std::mutex> lock(readerMutex_);
        if (albedoReader_) albedoReader_->close();
        if (normalReader_) normalReader_->close();
        if (ormReader_) ormReader_->close();
        albedoReader_.reset();
        normalReader_.reset();
        ormReader_.reset();
    }

    SVTTileData SVTFileTileProvider::generateTile(const VirtualTileCoord& coord)
    {
        std::lock_guard<std::mutex> lock(readerMutex_);

        SVTTileData result;
        result.coord = coord;
        result.valid = false;

        bool hasAny = false;

        if (albedoReader_ && albedoReader_->isValid())
        {
            if (albedoReader_->readTile(coord, result.albedoData))
                hasAny = true;
        }

        if (normalReader_ && normalReader_->isValid())
        {
            if (normalReader_->readTile(coord, result.normalData))
                hasAny = true;
        }

        if (ormReader_ && ormReader_->isValid())
        {
            if (ormReader_->readTile(coord, result.ormData))
                hasAny = true;
        }

        result.valid = hasAny;
        return result;
    }
}
