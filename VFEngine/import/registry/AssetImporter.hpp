#pragma once
#include <string>
#include <vector>
#include <span>
#include <cstdint>
#include "../pipeline/Pipeline.hpp"
#include "resource/AssetTypes.hpp"

namespace import
{
    struct DetectionInput
    {
        std::string_view path;
        std::span<const unsigned char> header; // first HEADER_SIZE bytes of the file
        uint64_t fileSize = 0;
    };

    struct FormatInfo
    {
        std::string fileType;                 // canonical id ("PNG") stored in ImportContext::fileType
        std::string displayName;              // file-dialog category ("Image Files")
        std::vector<std::string> extensions;  // lowercase, no dot ("png")
        std::string outputExtension;          // engine format written by process() ("vfImage")
        resource::AssetType assetType = resource::AssetType::COUNT; // COUNT => no .vfmeta sidecar
        int detectionPriority = 0;            // higher checked first; ties resolve by registration order
    };

    // One importer owns one or more formats. matches() and process() are called
    // concurrently from JobSystem workers and must be thread-safe (the same
    // contract the pre-registry types::* processors satisfied as shared members
    // of FileProcessingStage).
    class AssetImporter
    {
    public:
        virtual ~AssetImporter() = default;

        virtual std::vector<FormatInfo> formats() const = 0;

        // True if the input is of the given format (one of this importer's
        // fileType ids). Must not touch the registry.
        virtual bool matches(const std::string& fileType, const DetectionInput& input) const = 0;

        // context.fileType is one of this importer's formats. May throw;
        // FileProcessingStage converts exceptions into import failures.
        virtual void process(pipeline::ImportContext& context) = 0;

        // Output file name (with extension) when it depends on the import
        // config. Empty = default "<fileName>.<outputExtension>".
        virtual std::string deriveOutputFile(const pipeline::ImportContext&) const { return {}; }
    };

    // Adapts the per-file ImportProgressCallback to the single-float callbacks
    // the types::* processors take. The returned lambda captures context by
    // reference; it must not outlive the process() call.
    template<typename CallbackType>
    CallbackType wrapFileProgress(pipeline::ImportContext& context)
    {
        if (!context.progressCallback)
            return nullptr;

        return [&context](float progress)
        {
            context.progressCallback(context.fileName, context.fileIndex + 1,
                                     context.totalFiles, progress);
        };
    }
}
