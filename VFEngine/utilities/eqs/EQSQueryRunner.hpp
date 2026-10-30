#pragma once

#include "EQSQuery.hpp"
#include "EQSTypes.hpp"

namespace eqs
{
    // Runs one query synchronously, on the calling thread (VK-1660):
    //  1. generate - more than kMaxCandidates (EQSQuerySpec.hpp) candidates fails the query;
    //  2. every Filter test, in declared order: a candidate whose metric is kNoMetric or outside
    //     [filterMin, filterMax] is dropped, and later tests skip it;
    //  3. every Score test, in declared order: the metric is min-max normalised over the surviving candidates
    //     and shaped by the mode (Linear n, InverseLinear 1 - n, Square n*n, Constant 1) - a test whose metric
    //     is equal for all of them ranks nothing and scores 1 in every mode; kNoMetric scores 0 - then
    //     multiplied by the weight and combined (Multiply / Additive) into the total, which starts at 1 for
    //     every candidate;
    //  4. sorted by total, highest first, stable (ties keep generation order); the first maxResults are kept.
    // No surviving candidate = Completed with none. Never throws: an exception, or a generator or test that
    // returns false, is Failed with `error`. `debug` (optional) receives the same result plus every generated
    // candidate's metrics, scores and the filter that dropped it.
    EQSResult runQuery(const EQSQueryDef& def, const EQSContext& context, const EQSProviderRefs& providers,
                       EQSDebugResult* debug = nullptr);
}
