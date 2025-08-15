#pragma once
#include <string>

namespace files
{
    //change the utill lib name to commons
    class FileUtils
    {
    public:
        static std::string getFileExtension(std::string_view filePath, bool lowerCase = true);
        static std::string getFileName(std::string_view filePath);
        
        // UI helper methods - simple extension-based checking for UI purposes
        static bool isHDRFile(std::string_view filePath);
        static bool isTextureFile(std::string_view filePath);
        static bool isMeshFile(std::string_view filePath);
        static bool isAudioFile(std::string_view filePath);
    };
}
