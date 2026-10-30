#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "IEQSGenerator.hpp"
#include "IEQSTest.hpp"
#include "EQSTypes.hpp"

namespace eqs
{
    struct EQSTestEntry
    {
        std::shared_ptr<const IEQSTest> test;
        EQSTestConfig config;
    };

    // A runnable query, built from a template by buildQuery (EQSQuerySpec.hpp). Immutable once built.
    struct EQSQueryDef
    {
        std::string name;
        std::shared_ptr<const IEQSGenerator> generator;
        std::vector<EQSTestEntry> tests; // declared order: the runner applies every filter, then every scorer
        uint32_t maxResults = 1;
    };
}
