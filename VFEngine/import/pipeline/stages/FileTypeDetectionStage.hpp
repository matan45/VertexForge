#pragma once
#include "../Pipeline.hpp"

namespace pipeline::stages
{
    class FileTypeDetectionStage : public PipelineStage
    {
    public:
        std::optional<ImportContext> process(ImportContext context) override;
        std::string getName() const override { return "FileTypeDetection"; }

    private:
        // Image format detection
        bool isPNG(const std::vector<unsigned char>& header) const;
        bool isJPG(const std::vector<unsigned char>& header) const;
        bool isBMP(const std::vector<unsigned char>& header) const;
        bool isHDR(const std::vector<unsigned char>& header) const;
        bool isEXR(const std::vector<unsigned char>& header) const;

        // Audio format detection
        bool isMP3(const std::vector<unsigned char>& header) const;
        bool isWAV(const std::vector<unsigned char>& header) const;
        bool isOGG(const std::vector<unsigned char>& header) const;

        // Mesh format detection (improved)
        bool isOBJ(const std::vector<unsigned char>& header) const;
        bool isFBX(const std::vector<unsigned char>& header) const;
        bool isDAE(const std::vector<unsigned char>& header) const;
        bool isGLTF(const std::vector<unsigned char>& header) const;
        bool isGLB(const std::vector<unsigned char>& header) const;

        std::string detectFileType(const std::vector<unsigned char>& header) const;
    };
}