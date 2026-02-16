#include "FileValidationStage.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace pipeline::stages
{
    std::optional<ImportContext> FileValidationStage::process(ImportContext context)
    {
        // Extract filename
        fs::path fsPath(context.file.path.data());
        context.fileName = fsPath.stem().string();

        // Check if file exists
        if (!fs::exists(context.file.path.data()))
        {
            context.isValid = false;
            context.errorMessage = "File does not exist";
            return context;
        }

        // Check if file is readable
        std::ifstream file(context.file.path.data(), std::ios::binary);
        if (!file.is_open())
        {
            context.isValid = false;
            context.errorMessage = "Cannot open file for reading";
            return context;
        }

        // Check file size
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        if (fileSize <= 0)
        {
            context.isValid = false;
            context.errorMessage = "File is empty";
            return context;
        }

        return context;
    }
}