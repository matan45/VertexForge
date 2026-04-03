#pragma once

#include <resource/Types.hpp>
#include <config/Config.hpp>
#include <asset/AssetMetadata.hpp>
#include <functional>
#include <atomic>
#include <string>
#include <string_view>

namespace types
{
    using FractureProgressCallback = std::function<void(float progress, std::string_view stage)>;

    class FractureProcessor
    {
    public:
        struct Result
        {
            bool success = false;
            std::string errorMessage;
            resource::MeshesData fragmentMeshes;
            asset::FractureMetadata metadata;
        };

        static Result process(
            const resource::MeshData& inputMesh,
            const importConfig::FractureImportConfig& config,
            FractureProgressCallback progressCallback = nullptr,
            std::atomic<bool>* cancelFlag = nullptr);
    };
}
