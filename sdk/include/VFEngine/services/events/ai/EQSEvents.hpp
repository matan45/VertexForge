#pragma once
#include "../EventTypes.hpp"
#include "../../../utilities/eqs/EQSTypes.hpp"
#include "../../../utilities/eqs/EQSQuery.hpp"
#include <string>

namespace events::ai {

    struct RegisterEQSQueryCommand : ICommand<> {
        std::string queryName;
        eqs::EQSQueryDef queryDef;

        std::string_view getName() const override { return "RegisterEQSQuery"; }
    };

    struct SubmitEQSQueryCommand : ICommand<eqs::EQSQueryHandle> {
        std::string queryName;
        eqs::EQSContext context;

        std::string_view getName() const override { return "SubmitEQSQuery"; }
    };

    struct GetEQSQueryResultQuery : IQuery<eqs::EQSResult> {
        eqs::EQSQueryHandle handle;

        std::string_view getName() const override { return "GetEQSQueryResult"; }
    };

    struct CancelEQSQueryCommand : ICommand<> {
        eqs::EQSQueryHandle handle;

        std::string_view getName() const override { return "CancelEQSQuery"; }
    };

}
