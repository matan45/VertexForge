#include "FileProcessingStage.hpp"
#include "../../registry/ImporterRegistry.hpp"
#include <stdexcept>

namespace pipeline::stages
{
    std::optional<ImportContext> FileProcessingStage::process(ImportContext context)
    {
        auto* importer = import::ImporterRegistry::instance().importerFor(context.fileType);
        if (!importer)
        {
            context.isValid = false;
            context.errorMessage = "Unsupported file type for processing: " + context.fileType;
            return context;
        }

        try
        {
            importer->process(context);
        }
        catch (const std::exception& e)
        {
            context.isValid = false;
            context.errorMessage = "Processing failed: " + std::string(e.what());
            return context;
        }

        return context;
    }
}
