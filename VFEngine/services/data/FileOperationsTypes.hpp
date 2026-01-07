#pragma once
#include <string>
#include <vector>

namespace services
{
    enum class FileOperationType
    {
        Move,
        Copy,
        Delete,
        Rename,
        CreateFolder
    };

    struct FileOperationResult
    {
        bool success = false;
        std::string errorMessage;
        std::vector<std::string> updatedReferences;  // Files that had their references updated
        std::vector<std::string> conflicts;          // Conflicting file paths (duplicates, etc.)
    };

    enum class ConflictResolution
    {
        Skip,
        Overwrite,
        Rename
    };
}
