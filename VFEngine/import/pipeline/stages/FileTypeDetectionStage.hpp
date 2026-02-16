#pragma once
#include "../Pipeline.hpp"
#include <span>

namespace pipeline::stages
{
    class FileTypeDetectionStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileTypeDetection"; }

    private:
        struct SignatureEntry
        {
            std::span<const unsigned char> signature;
            const char* fileType;
        };

        static bool matchesSignature(const std::vector<unsigned char>& header,
                                     std::span<const unsigned char> signature);

        // Complex detection methods that need multi-step logic
        bool isHDR(const std::vector<unsigned char>& header) const;
        bool isMP3(const std::vector<unsigned char>& header) const;
        bool isWAV(const std::vector<unsigned char>& header) const;
        bool isFBX(const std::vector<unsigned char>& header) const;
        bool isDAE(const std::vector<unsigned char>& header) const;
        bool isGLTF(const std::vector<unsigned char>& header) const;
        bool isOBJ(const std::vector<unsigned char>& header) const;
        bool isTTF(const std::vector<unsigned char>& header) const;
        bool isTGA(const std::vector<unsigned char>& header) const;

        std::string detectFileType(const std::vector<unsigned char>& header) const;
    };
}
