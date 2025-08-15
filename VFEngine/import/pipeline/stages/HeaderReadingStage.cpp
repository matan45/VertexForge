#include "HeaderReadingStage.hpp"
#include <fstream>
#include <bit>

namespace pipeline::stages
{
    std::optional<ImportContext> HeaderReadingStage::process(ImportContext context)
    {
        std::ifstream file(context.file.path.data(), std::ios::binary);
        if (!file.is_open())
        {
            context.isValid = false;
            context.errorMessage = "Failed to open file for header reading";
            return context;
        }

        context.header.resize(HEADER_SIZE);
        file.read(std::bit_cast<char*>(context.header.data()), HEADER_SIZE);
        
        // Check how many bytes were actually read
        auto bytesRead = file.gcount();
        if (bytesRead <= 0)
        {
            context.isValid = false;
            context.errorMessage = "Failed to read file header";
            return context;
        }

        // Resize to actual bytes read
        context.header.resize(static_cast<size_t>(bytesRead));
        file.close();

        return context;
    }
}