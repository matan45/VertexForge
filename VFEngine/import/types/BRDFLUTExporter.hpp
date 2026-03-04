#pragma once
#include <string>

namespace types
{
    class BRDFLUTExporter
    {
    public:
        static void generateAndSave(const std::string& outputPath);
    };
}
