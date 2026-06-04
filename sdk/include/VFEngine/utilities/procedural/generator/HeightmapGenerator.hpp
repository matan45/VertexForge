#pragma once
#pragma warning(disable: 4251)

#include "../ProceduralExport.hpp"
#include "HeightmapParams.hpp"
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace procedural
{
    using ProgressCallback = std::function<void(float progress)>;

    struct VF_PROCEDURAL_API HeightmapResult
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgbaData; // R=G=B=height, A=255

        bool valid() const { return !rgbaData.empty(); }
    };

    class VF_PROCEDURAL_API HeightmapGenerator
    {
    public:
        // Generate full-resolution heightmap (call from background thread)
        static HeightmapResult generate(const HeightmapParams& params,
                                        ProgressCallback progress = nullptr);

        // Generate low-res 256x256 preview (fast, can be called on UI thread)
        static HeightmapResult generatePreview(const HeightmapParams& params);

        // Write result as uncompressed .vfImage
        static bool saveAsVFImage(const HeightmapResult& result,
                                  const std::string& outputPath);
    };
}
