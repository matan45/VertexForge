#pragma once

#include "animator/SocketTypes.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace types
{
    class MeshSocketWriter
    {
    public:
        static bool saveSocketsToMesh(const std::string& meshPath,
                                       const std::vector<animator::SocketDefinition>& sockets);

    private:
        static std::streampos findSocketOffset(const std::string& meshPath);
        static bool readFilePrefix(const std::string& meshPath, std::streampos offset,
                                   std::vector<char>& outData);
        static bool writeSocketFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                    const std::vector<animator::SocketDefinition>& sockets);
    };
}
