#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include "IEQSGenerator.hpp"
#include "IEQSTest.hpp"
#include "EQSTypes.hpp"

namespace eqs
{
    struct EQSTestEntry
    {
        std::shared_ptr<IEQSTest> test;
        EQSTestConfig config;
    };

    struct EQSQueryDef
    {
        std::string name;
        std::shared_ptr<IEQSGenerator> generator;
        std::vector<EQSTestEntry> tests;
        uint32_t maxResults = 1;
    };
}
