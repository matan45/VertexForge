#include "EQSQueryLibrary.hpp"
#include <exception>
#include <format>
#include <utility>

namespace eqs
{
    namespace
    {
        EQSQueryLibrary::Lookup failedLookup(bool exists, std::string message)
        {
            EQSQueryLibrary::Lookup lookup;
            lookup.exists = exists;
            lookup.errors.push_back(EQSSpecError{std::string(), std::move(message)});
            return lookup;
        }
    }

    EQSQueryLibrary::EQSQueryLibrary(Loader loader)
        : loader(std::move(loader))
    {
    }

    EQSQueryLibrary::Lookup EQSQueryLibrary::lookup(const std::string& name)
    {
        if (!isValidQueryName(name))
        {
            return failedLookup(false, std::format("'{}' is not a valid EQS query name (a letter, then up to 63 "
                                                   "letters, digits or '_')", name));
        }

        {
            std::lock_guard lock(mutex);
            if (auto it = cache.find(name); it != cache.end())
                return it->second;
        }

        // Read and build outside the lock; a concurrent lookup of the same name may do the same work, and the
        // last one stores its equal result.
        try
        {
            const std::optional<std::string> text = loader ? loader(name) : std::nullopt;
            if (!text)
                return failedLookup(false, std::format("there is no EQS query template named '{}'", name)); // not cached

            Lookup result;
            result.exists = true;
            EQSSpecParseResult parsed = parseQuerySpecText(*text);
            if (parsed.spec)
            {
                result.def = std::make_shared<const EQSQueryDef>(buildQuery(*parsed.spec, name));
                result.spec = std::make_shared<const EQSQuerySpec>(std::move(*parsed.spec));
            }
            else
            {
                result.errors = std::move(parsed.errors);
            }

            std::lock_guard lock(mutex);
            cache[name] = result;
            return result;
        }
        catch (const std::exception& exception)
        {
            return failedLookup(false, std::format("reading the EQS query '{}' failed: {}", name, exception.what()));
        }
        catch (...)
        {
            return failedLookup(false, std::format("reading the EQS query '{}' failed", name));
        }
    }

    void EQSQueryLibrary::invalidate(const std::string& name)
    {
        std::lock_guard lock(mutex);
        if (name.empty())
            cache.clear();
        else
            cache.erase(name);
    }
}
