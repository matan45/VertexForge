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

    // Declarative per-file import option: the import dialog renders these
    // generically (checkbox / slider / combo) and stores the chosen values in
    // ImportConfig::customOptions under `key`, so importers get config UI
    // without any editor code.
    struct ImportOptionDesc
    {
        std::string key;
        std::string label;
        std::string tooltip;
        enum class Type { Bool, Int, Float, Enum } type = Type::Bool;
        importConfig::ImportOptionValue defaultValue = false;
        float minValue = 0.0f;                // Int/Float slider range
        float maxValue = 0.0f;
        std::vector<std::string> enumNames;   // Enum entries (value stored as int32_t index)
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

        // Asset type for the .vfmeta sidecar when it depends on the import
        // config. COUNT = use the format's assetType.
        virtual resource::AssetType deriveAssetType(const pipeline::ImportContext&) const
        {
            return resource::AssetType::COUNT;
        }

        // Per-file options shown in the import dialog for this importer's
        // extensions; chosen values arrive in context.file.config.customOptions.
        virtual std::vector<ImportOptionDesc> options() const { return {}; }
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
