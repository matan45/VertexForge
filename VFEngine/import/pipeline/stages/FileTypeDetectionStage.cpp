#include "FileTypeDetectionStage.hpp"
#include <algorithm>
#include <ranges>

namespace pipeline::stages
{
    std::optional<ImportContext> FileTypeDetectionStage::process(ImportContext context)
    {
        context.fileType = detectFileType(context.header);
        
        if (context.fileType == "Unknown")
        {
            context.isValid = false;
            context.errorMessage = "Unsupported file format";
            return context;
        }

        return context;
    }

    std::string FileTypeDetectionStage::detectFileType(const std::vector<unsigned char>& header) const
    {
        // Image formats
        if (isPNG(header)) return "PNG";
        if (isJPG(header)) return "JPEG";
        if (isBMP(header)) return "BMP";
        if (isHDR(header)) return "HDR";
        if (isEXR(header)) return "EXR";

        // Audio formats
        if (isMP3(header)) return "MP3";
        if (isWAV(header)) return "WAV";
        if (isOGG(header)) return "OGG";

        // Mesh formats
        if (isGLB(header)) return "GLB";
        if (isFBX(header)) return "FBX";
        if (isDAE(header)) return "DAE";
        if (isGLTF(header)) return "GLTF";
        if (isOBJ(header)) return "OBJ";

        // Font formats
        if (isTTF(header)) return "TTF";
        if (isOTF(header)) return "OTF";

        return "Unknown";
    }

    bool FileTypeDetectionStage::isPNG(const std::vector<unsigned char>& header) const
    {
        const unsigned char pngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        return header.size() >= 8 && std::equal(header.begin(), header.begin() + 8, pngSignature);
    }

    bool FileTypeDetectionStage::isJPG(const std::vector<unsigned char>& header) const
    {
        const unsigned char jpgSignature[3] = {0xFF, 0xD8, 0xFF};
        return header.size() >= 3 && std::equal(header.begin(), header.begin() + 3, jpgSignature);
    }

    bool FileTypeDetectionStage::isBMP(const std::vector<unsigned char>& header) const
    {
        const unsigned char bmpSignature[2] = {0x42, 0x4D}; // 'BM'
        return header.size() >= 2 && std::equal(header.begin(), header.begin() + 2, bmpSignature);
    }

    bool FileTypeDetectionStage::isHDR(const std::vector<unsigned char>& header) const
    {
        const std::string hdrSignature1 = "#?RADIANCE";
        const std::string hdrSignature2 = "#?RGBE";
        
        if (header.size() < hdrSignature1.size()) return false;
        
        return std::equal(header.begin(), header.begin() + hdrSignature1.size(), hdrSignature1.begin()) ||
               (header.size() >= hdrSignature2.size() && 
                std::equal(header.begin(), header.begin() + hdrSignature2.size(), hdrSignature2.begin()));
    }

    bool FileTypeDetectionStage::isEXR(const std::vector<unsigned char>& header) const
    {
        const unsigned char exrSignature[4] = {0x76, 0x2F, 0x31, 0x01}; // 'v/1' + 0x01
        auto result = std::ranges::search(header, exrSignature);
        return !result.empty();
    }

    bool FileTypeDetectionStage::isMP3(const std::vector<unsigned char>& header) const
    {
        return header.size() >= 3 &&
            ((header[0] == 0x49 && header[1] == 0x44 && header[2] == 0x33) || // 'ID3'
             (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)); // Frame sync bytes
    }

    bool FileTypeDetectionStage::isWAV(const std::vector<unsigned char>& header) const
    {
        return header.size() >= 12 &&
            header[0] == 0x52 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x46 && // 'RIFF'
            header[8] == 0x57 && header[9] == 0x41 && header[10] == 0x56 && header[11] == 0x45; // 'WAVE'
    }

    bool FileTypeDetectionStage::isOGG(const std::vector<unsigned char>& header) const
    {
        const unsigned char oggSignature[4] = {0x4F, 0x67, 0x67, 0x53}; // 'OggS'
        return header.size() >= 4 && std::equal(header.begin(), header.begin() + 4, oggSignature);
    }

    bool FileTypeDetectionStage::isGLB(const std::vector<unsigned char>& header) const
    {
        // GLB (binary glTF) starts with 'glTF' magic number
        const unsigned char glbSignature[4] = {0x67, 0x6C, 0x54, 0x46}; // 'glTF'
        return header.size() >= 4 && std::equal(header.begin(), header.begin() + 4, glbSignature);
    }

    bool FileTypeDetectionStage::isFBX(const std::vector<unsigned char>& header) const
    {
        // Binary FBX files start with specific signature
        const std::string fbxSignature = "Kaydara FBX Binary";
        if (header.size() < fbxSignature.size()) return false;
        
        auto result = std::ranges::search(header, fbxSignature);
        return !result.empty();
    }

    bool FileTypeDetectionStage::isDAE(const std::vector<unsigned char>& header) const
    {
        // COLLADA files are XML, look for COLLADA root element
        const std::string xmlStart = "<?xml";
        const std::string colladaRoot = "<COLLADA";
        
        if (header.size() < xmlStart.size()) return false;
        
        auto xmlResult = std::ranges::search(header, xmlStart);
        if (xmlResult.empty()) return false;
        
        auto colladaResult = std::ranges::search(header, colladaRoot);
        return !colladaResult.empty();
    }

    bool FileTypeDetectionStage::isGLTF(const std::vector<unsigned char>& header) const
    {
        // JSON glTF files start with '{' and contain "asset" field
        if (header.empty() || header[0] != '{') return false;
        
        const std::string gltfAsset = "\"asset\"";
        auto result = std::ranges::search(header, gltfAsset);
        return !result.empty();
    }

    bool FileTypeDetectionStage::isOBJ(const std::vector<unsigned char>& header) const
    {
        // OBJ files are text-based, look for common OBJ keywords
        const std::vector<std::string> objKeywords = {"# ", "v ", "vn ", "vt ", "f ", "o ", "g "};

        std::string headerStr(header.begin(), header.end());

        for (const auto& keyword : objKeywords)
        {
            if (headerStr.find(keyword) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    bool FileTypeDetectionStage::isTTF(const std::vector<unsigned char>& header) const
    {
        if (header.size() < 4) return false;

        // Standard TrueType: 00 01 00 00
        if (header[0] == 0x00 && header[1] == 0x01 &&
            header[2] == 0x00 && header[3] == 0x00)
        {
            return true;
        }

        // Apple TrueType: 'true' (74 72 75 65)
        if (header[0] == 0x74 && header[1] == 0x72 &&
            header[2] == 0x75 && header[3] == 0x65)
        {
            return true;
        }

        // TrueType Collection: 'ttcf' (74 74 63 66)
        if (header[0] == 0x74 && header[1] == 0x74 &&
            header[2] == 0x63 && header[3] == 0x66)
        {
            return true;
        }

        return false;
    }

    bool FileTypeDetectionStage::isOTF(const std::vector<unsigned char>& header) const
    {
        // OpenType with CFF: 'OTTO' (4F 54 54 4F)
        return header.size() >= 4 &&
               header[0] == 0x4F && header[1] == 0x54 &&
               header[2] == 0x54 && header[3] == 0x4F;
    }
}