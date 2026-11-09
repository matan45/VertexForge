#pragma once
#include <eqs/EQSQuerySpec.hpp>
#include <cstdint>
#include <string>
#include <vector>

// VK-1660: data carried by the EQS events (services/events/ai/EQSEvents.hpp, EQSAuthoringEvents.hpp).
// Header-only value types.
namespace services
{
    // One query template, assets/eqs/<name>.vfEQSQuery.
    struct EQSQueryInfo
    {
        std::string name;
        std::string path;                      // the absolute path it is looked up at
        bool exists = false;
        bool valid = false;
        std::vector<eqs::EQSSpecError> errors; // why it is invalid (or missing)
        std::string description;               // the template's "description"
        std::string summary;                   // eqs::summarizeQuery
    };

    struct EQSAssetLoadResult
    {
        bool success = false;
        std::string error;
        std::string text;         // the file's text
        uint64_t contentHash = 0; // hash of the file bytes
    };

    struct EQSAssetSaveResult
    {
        bool success = false;
        std::string error;                     // why the save was refused (path, guard, spec or I/O)
        std::vector<eqs::EQSSpecError> errors; // the spec errors that refused it
        bool created = false;                  // the file did not exist before this save
        uint64_t contentHash = 0;              // hash of the bytes now on disk
    };
}
