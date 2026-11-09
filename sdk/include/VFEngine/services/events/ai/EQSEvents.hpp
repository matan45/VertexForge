#pragma once
#include "../EventTypes.hpp"
#include "../../data/EQSData.hpp"
#include "../../../utilities/eqs/EQSTypes.hpp"
#include <string>
#include <string_view>
#include <vector>

// EQS runtime events (rewritten for VK-1660). Templates are assets/eqs/<Name>.vfEQSQuery
// (utilities/eqs/EQSQuerySpec.hpp). Submitted queries run in the EQS frame task during Play;
// RunEQSQueryNowQuery runs one at once for tools. Handled by services::EQSServiceImpl.
namespace events::ai {

    // Queues the template's query. The result is ready one frame later at the earliest; release the handle
    // (CancelEQSQueryCommand) once it is read. Invalid handle when the template is missing or invalid, or the
    // queue is full.
    struct SubmitEQSQueryCommand : ICommand<eqs::EQSQueryHandle> {
        std::string queryName;
        eqs::EQSContext context;

        std::string_view getName() const override { return "SubmitEQSQuery"; }
    };

    // Pending / Running / Completed / Failed. An unknown, released or expired handle reads as Failed.
    struct GetEQSQueryResultQuery : IQuery<eqs::EQSResult> {
        eqs::EQSQueryHandle handle;

        std::string_view getName() const override { return "GetEQSQueryResult"; }
    };

    // Cancels a queued query, or frees its finished result.
    struct CancelEQSQueryCommand : ICommand<> {
        eqs::EQSQueryHandle handle;

        std::string_view getName() const override { return "CancelEQSQuery"; }
    };

    // Runs the template now, on the calling thread, with every candidate's detail (MCP eqs_run, main thread
    // between frames). Never queued; works in any mode. A missing or invalid template is a Failed result.
    struct RunEQSQueryNowQuery : IQuery<eqs::EQSDebugResult> {
        std::string queryName;
        eqs::EQSContext context;

        std::string_view getName() const override { return "RunEQSQueryNow"; }
    };

    // Every template in the project's assets/eqs folder, sorted by name.
    struct ListEQSQueriesQuery : IQuery<std::vector<services::EQSQueryInfo>> {
        std::string_view getName() const override { return "ListEQSQueries"; }
    };

    struct GetEQSQueryInfoQuery : IQuery<services::EQSQueryInfo> {
        std::string queryName;

        std::string_view getName() const override { return "GetEQSQueryInfo"; }
    };

    // Draws a run's candidates for `seconds` (debug draw, any mode; a new command replaces the previous one):
    // kept candidates colored by score with the best one larger, dropped ones gray.
    struct ShowEQSDebugCommand : ICommand<> {
        eqs::EQSDebugResult result;
        float seconds = 10.0f;

        std::string_view getName() const override { return "ShowEQSDebug"; }
    };

}
