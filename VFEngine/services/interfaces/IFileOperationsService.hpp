#pragma once
#include "../data/FileOperationsTypes.hpp"
#include <string>

namespace services
{
    class IFileOperationsService
    {
    public:
        virtual ~IFileOperationsService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        virtual FileOperationResult moveFile(const std::string& sourcePath, const std::string& destPath) = 0;

        virtual FileOperationResult copyFile(const std::string& sourcePath, const std::string& destPath) = 0;

        virtual FileOperationResult deleteFile(const std::string& path) = 0;
    };
}
