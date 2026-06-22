#pragma once

#include "animator/SocketTypes.hpp"
#include "../ImportExport.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace types
{
    class VF_IMPORT_API MeshSocketWriter
    {
    public:
        static bool saveSocketsToMesh(const std::string& meshPath,
                                       const std::vector<animator::SocketDefinition>& sockets);

    private:
        // Returns the offset where the socket block begins. outSuffixOffset receives the offset
        // where any trailing block (IK chains, etc.) begins — i.e. the end of the existing socket
        // block — or 0 when there is nothing after the socket block (e.g. static meshes).
        static std::streampos findSocketOffset(const std::string& meshPath, std::streampos& outSuffixOffset);
        static bool readFilePrefix(const std::string& meshPath, std::streampos offset,
                                   std::vector<char>& outData);
        // Reads [offset, EOF) — the data that must survive a socket rewrite (IK chains, etc.).
        static bool readFileSuffix(const std::string& meshPath, std::streampos offset,
                                   std::vector<char>& outData);
        static bool writeSocketFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                    const std::vector<animator::SocketDefinition>& sockets,
                                    const std::vector<char>& suffixData);
    };
}
