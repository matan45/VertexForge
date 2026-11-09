#pragma once
#include "../../data/EQSData.hpp"
#include "../../../utilities/eqs/EQSTypes.hpp"
#include <string>

namespace services
{
    // The engine side of EQS (rewritten for VK-1660): the template library (assets/eqs/<Name>.vfEQSQuery),
    // the in-frame scheduler and the engine providers (navmesh, physics line of sight, entities).
    class IEQSProvider
    {
    public:
        virtual ~IEQSProvider() = default;

        // Invalid handle when the template is missing or invalid, or the queue is full.
        virtual eqs::EQSQueryHandle submitQuery(const std::string& queryName, const eqs::EQSContext& context) = 0;
        virtual eqs::EQSResult getResult(eqs::EQSQueryHandle handle) const = 0;
        virtual void releaseQuery(eqs::EQSQueryHandle handle) = 0;
        virtual void releaseAll() = 0;

        // Runs queued queries - whole queries, first in first out - while the time spent stays under budgetMs.
        // Called from the EQS frame task.
        virtual void update(float budgetMs) = 0;

        // Runs a template at once, on the calling thread, with every candidate's detail. A missing or invalid
        // template is a Failed result with the reason.
        virtual eqs::EQSDebugResult runQueryNow(const std::string& queryName, const eqs::EQSContext& context) = 0;

        // The template's state; `path` is where it is looked up.
        virtual EQSQueryInfo lookupQuery(const std::string& queryName) = 0;

        // Forgets the cached template so its next use reads the file again; "" = every template.
        virtual void invalidateQuery(const std::string& queryName) = 0;
    };
}
