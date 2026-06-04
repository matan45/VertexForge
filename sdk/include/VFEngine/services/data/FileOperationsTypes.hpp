#pragma once
#include <string>
#include <vector>

namespace services
{
    struct FileOperationResult
    {
        bool success = false;
        std::string errorMessage;
        std::vector<std::string> updatedReferences; 
        std::vector<std::string> conflicts;
    };
}
