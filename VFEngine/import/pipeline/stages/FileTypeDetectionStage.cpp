#include "FileTypeDetectionStage.hpp"
#include "../../registry/ImporterRegistry.hpp"
#include "../../registry/builtin/BuiltinImporters.hpp"
#include <filesystem>

namespace pipeline::stages
{
    std::optional<ImportContext> FileTypeDetectionStage::process(ImportContext context)
    {
        import::builtin::ensureRegistered();

        std::error_code ec;
        uint64_t fileSize = std::filesystem::file_size(context.file.path, ec);
        if (ec) fileSize = 0;

        const import::DetectionInput input{context.file.path,
                                           std::span<const unsigned char>(context.header),
                                           fileSize};
        context.fileType = import::ImporterRegistry::instance().detect(input);

        if (context.fileType == "Unknown")
        {
            context.isValid = false;
            context.errorMessage = "Unsupported file format";
            return context;
        }

        return context;
    }
}
