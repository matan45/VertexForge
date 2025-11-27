#pragma once
#include <vector>
#include <string>
#include <memory>
#include "config/Config.hpp"
#include "../pipeline/Pipeline.hpp"

namespace controllers
{
    class Import
    {
    private:
        inline static std::string location;
        inline static std::unique_ptr<pipeline::ImportPipeline> importPipeline;

    public:
        static void importFiles(const std::vector<importConfig::ImportFiles>& paths);
        static void setLocation(std::string_view newLocation);
        static void initialize();

    private:
        static void setupPipeline();
        static void waitForCompletion(std::vector<std::future<std::optional<pipeline::ImportContext>>>&& futures);
    };
}
