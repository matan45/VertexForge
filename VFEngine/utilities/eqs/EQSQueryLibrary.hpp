#pragma once

#include "EQSQuery.hpp"
#include "EQSQuerySpec.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eqs
{
    // Query templates by name (VK-1660): reads assets/eqs/<Name>.vfEQSQuery through the injected loader,
    // parses it strictly (parseQuerySpecText) and caches the built query until invalidated. Thread-safe.
    class EQSQueryLibrary
    {
    public:
        // The template text for `name`; nullopt when there is no such file.
        using Loader = std::function<std::optional<std::string>(const std::string& name)>;

        struct Lookup
        {
            bool exists = false;                      // the loader found a file
            std::shared_ptr<const EQSQueryDef> def;   // set when the template is valid
            std::shared_ptr<const EQSQuerySpec> spec; // set when the template is valid
            // Why there is no def: the spec errors, or one error at path "" for an invalid name or a missing
            // file.
            std::vector<EQSSpecError> errors;
        };

        explicit EQSQueryLibrary(Loader loader);

        // An existing template (valid or not) is cached until invalidate(name) / invalidate(""); a missing one
        // is looked up again every time, so a file added later is found. Never throws.
        Lookup lookup(const std::string& name);

        // "" = every template.
        void invalidate(const std::string& name);

    private:
        Loader loader;
        std::mutex mutex;
        std::unordered_map<std::string, Lookup> cache;
    };
}
