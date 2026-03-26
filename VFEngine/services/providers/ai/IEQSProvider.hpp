#pragma once
#include "../../../utilities/eqs/EQSTypes.hpp"
#include "../../../utilities/eqs/EQSQuery.hpp"
#include <string>

namespace services
{
    class IEQSProvider
    {
    public:
        virtual ~IEQSProvider() = default;

        virtual void registerQuery(const std::string& name, const eqs::EQSQueryDef& def) = 0;
        virtual eqs::EQSQueryHandle submitQuery(const std::string& queryName, const eqs::EQSContext& context) = 0;
        virtual eqs::EQSResult getResult(eqs::EQSQueryHandle handle) const = 0;
        virtual void cancelQuery(eqs::EQSQueryHandle handle) = 0;
        virtual void update(float frameBudgetMs = 2.0f) = 0;
    };
}
