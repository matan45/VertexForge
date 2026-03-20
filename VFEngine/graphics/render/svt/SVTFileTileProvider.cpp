#include "SVTFileTileProvider.hpp"
#include "print/Log.hpp"

namespace render::svt
{
    bool SVTFileTileProvider::openFiles(const std::string& albedoPath,
                                         const std::string& normalPath,
                                         const std::string& ormPath)
    {
        std::lock_guard<std::mutex> lock(readerMutex);

        if (!albedoPath.empty())
        {
            albedoReader = std::make_unique<SVTFileReader>();
            if (!albedoReader->open(albedoPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open albedo SVT: {}", albedoPath);
                albedoReader.reset();
            }
        }

        if (!normalPath.empty())
        {
            normalReader = std::make_unique<SVTFileReader>();
            if (!normalReader->open(normalPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open normal SVT: {}", normalPath);
                normalReader.reset();
            }
        }

        if (!ormPath.empty())
        {
            ormReader = std::make_unique<SVTFileReader>();
            if (!ormReader->open(ormPath))
            {
                vfLogWarning("SVTFileTileProvider: Failed to open ORM SVT: {}", ormPath);
                ormReader.reset();
            }
        }

        return albedoReader || normalReader || ormReader;
    }

    void SVTFileTileProvider::close()
    {
        std::lock_guard<std::mutex> lock(readerMutex);
        if (albedoReader) albedoReader->close();
        if (normalReader) normalReader->close();
        if (ormReader) ormReader->close();
        albedoReader.reset();
        normalReader.reset();
        ormReader.reset();
    }

    SVTTileData SVTFileTileProvider::generateTile(const VirtualTileCoord& coord)
    {
        std::lock_guard<std::mutex> lock(readerMutex);

        SVTTileData result;
        result.coord = coord;
        result.valid = false;

        bool hasAny = false;

        if (albedoReader && albedoReader->isValid())
        {
            if (albedoReader->readTile(coord, result.albedoData))
                hasAny = true;
        }

        if (normalReader && normalReader->isValid())
        {
            if (normalReader->readTile(coord, result.normalData))
                hasAny = true;
        }

        if (ormReader && ormReader->isValid())
        {
            if (ormReader->readTile(coord, result.ormData))
                hasAny = true;
        }

        result.valid = hasAny;
        return result;
    }
}
