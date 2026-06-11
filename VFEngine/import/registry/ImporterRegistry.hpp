#pragma once
#include <memory>
#include <optional>
#include <shared_mutex>
#include "AssetImporter.hpp"
#include "../ImportExport.hpp"

namespace import
{
#pragma warning(push)
#pragma warning(disable: 4251) // private members don't need dll-interface
    // Single source of truth for file-type detection, processing dispatch,
    // output extensions and asset-type mapping. Registration happens during
    // initialization (engine builtins, then plugins); lookups run concurrently
    // from JobSystem import workers under a shared lock. Unregistration must
    // only happen while no import is in flight.
    class VF_IMPORT_API ImporterRegistry
    {
    public:
        static ImporterRegistry& instance();

        ImporterRegistry() = default;
        ImporterRegistry(const ImporterRegistry&) = delete;
        ImporterRegistry& operator=(const ImporterRegistry&) = delete;

        void registerImporter(std::unique_ptr<AssetImporter> importer, std::string_view ownerTag = "engine");
        void unregisterByOwner(std::string_view ownerTag);
        bool hasOwner(std::string_view ownerTag) const;

        // Returns the matched fileType, or "Unknown".
        std::string detect(const DetectionInput& input) const;
        AssetImporter* importerFor(const std::string& fileType) const;
        std::optional<FormatInfo> formatInfo(const std::string& fileType) const;
        std::vector<FormatInfo> allFormats() const;

    private:
        struct Registration
        {
            std::unique_ptr<AssetImporter> importer;
            std::string owner;
            std::vector<FormatInfo> formats;
            uint64_t order = 0;
        };

        struct DetectionEntry
        {
            int priority = 0;
            uint64_t order = 0;
            const FormatInfo* format = nullptr;
            AssetImporter* importer = nullptr;
        };

        void rebuildDetectionOrder(); // caller holds unique_lock

        mutable std::shared_mutex mutex;
        std::vector<Registration> registrations;
        std::vector<DetectionEntry> detectionOrder;
        uint64_t nextOrder = 0;
    };
#pragma warning(pop)
}
