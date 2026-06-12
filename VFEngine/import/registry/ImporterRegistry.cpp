#include "ImporterRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace import
{
    ImporterRegistry& ImporterRegistry::instance()
    {
        static ImporterRegistry registry;
        return registry;
    }

    void ImporterRegistry::registerImporter(std::unique_ptr<AssetImporter> importer, std::string_view ownerTag)
    {
        if (!importer)
            return;

        std::unique_lock lock(mutex);

        Registration registration;
        registration.formats = importer->formats();
        registration.importer = std::move(importer);
        registration.owner = ownerTag;
        registration.order = nextOrder++;

        for (const auto& format : registration.formats)
        {
            vfLogInfo("Registered importer format {} -> .{} (owner: {})",
                      format.fileType, format.outputExtension, registration.owner);
        }

        registrations.push_back(std::move(registration));
        rebuildDetectionOrder();
    }

    void ImporterRegistry::unregisterByOwner(std::string_view ownerTag)
    {
        std::unique_lock lock(mutex);

        auto removed = std::erase_if(registrations, [&](const Registration& registration)
        {
            return registration.owner == ownerTag;
        });

        if (removed > 0)
        {
            vfLogInfo("Unregistered {} importer(s) owned by {}", removed, ownerTag);
            rebuildDetectionOrder();
        }
    }

    void ImporterRegistry::unregisterAllExceptOwner(std::string_view ownerTag)
    {
        std::unique_lock lock(mutex);

        auto removed = std::erase_if(registrations, [&](const Registration& registration)
        {
            return registration.owner != ownerTag;
        });

        if (removed > 0)
        {
            vfLogInfo("Unregistered {} importer(s) not owned by {}", removed, ownerTag);
            rebuildDetectionOrder();
        }
    }

    std::vector<ImportOptionDesc> ImporterRegistry::optionsForExtension(const std::string& extension) const
    {
        std::shared_lock lock(mutex);

        std::vector<ImportOptionDesc> options;
        for (const auto& entry : detectionOrder)
        {
            const auto& extensions = entry.format->extensions;
            if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
                continue;

            for (auto& desc : entry.importer->options())
            {
                bool seen = std::ranges::any_of(options, [&](const ImportOptionDesc& existing)
                {
                    return existing.key == desc.key;
                });
                if (!seen)
                    options.push_back(std::move(desc));
            }
        }
        return options;
    }

    bool ImporterRegistry::hasOwner(std::string_view ownerTag) const
    {
        std::shared_lock lock(mutex);
        return std::ranges::any_of(registrations, [&](const Registration& registration)
        {
            return registration.owner == ownerTag;
        });
    }

    std::string ImporterRegistry::detect(const DetectionInput& input) const
    {
        std::shared_lock lock(mutex);

        for (const auto& entry : detectionOrder)
        {
            if (entry.importer->matches(entry.format->fileType, input))
                return entry.format->fileType;
        }

        return "Unknown";
    }

    AssetImporter* ImporterRegistry::importerFor(const std::string& fileType) const
    {
        std::shared_lock lock(mutex);

        for (const auto& registration : registrations)
        {
            for (const auto& format : registration.formats)
            {
                if (format.fileType == fileType)
                    return registration.importer.get();
            }
        }

        return nullptr;
    }

    std::optional<FormatInfo> ImporterRegistry::formatInfo(const std::string& fileType) const
    {
        std::shared_lock lock(mutex);

        for (const auto& registration : registrations)
        {
            for (const auto& format : registration.formats)
            {
                if (format.fileType == fileType)
                    return format;
            }
        }

        return std::nullopt;
    }

    std::vector<FormatInfo> ImporterRegistry::allFormats() const
    {
        std::shared_lock lock(mutex);

        std::vector<FormatInfo> formats;
        for (const auto& registration : registrations)
        {
            formats.insert(formats.end(), registration.formats.begin(), registration.formats.end());
        }
        return formats;
    }

    void ImporterRegistry::rebuildDetectionOrder()
    {
        detectionOrder.clear();

        for (const auto& registration : registrations)
        {
            for (const auto& format : registration.formats)
            {
                detectionOrder.push_back({format.detectionPriority, registration.order,
                                          &format, registration.importer.get()});
            }
        }

        // Higher priority first; ties keep registration order, then in-importer
        // declaration order (stable_sort preserves it).
        std::ranges::stable_sort(detectionOrder, [](const DetectionEntry& a, const DetectionEntry& b)
        {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.order < b.order;
        });
    }
}
