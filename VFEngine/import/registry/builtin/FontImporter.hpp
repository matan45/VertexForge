#pragma once
#include "../AssetImporter.hpp"
#include "../../types/Font.hpp"

namespace import::builtin
{
    // TTF / OTF -> .vfFont
    class FontImporter : public AssetImporter
    {
    public:
        std::vector<FormatInfo> formats() const override;
        bool matches(const std::string& fileType, const DetectionInput& input) const override;
        void process(pipeline::ImportContext& context) override;
        std::vector<ImportOptionDesc> options() const override;

    private:
        types::Font fontProcessor;
    };
}
