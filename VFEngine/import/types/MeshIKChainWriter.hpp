#pragma once

#include "components/IKComponent.hpp"
#include <fstream>
#include <string>
#include <vector>

namespace types
{
    class MeshIKChainWriter
    {
    public:
        static bool saveIKChainsToMesh(const std::string& meshPath,
                                        const std::vector<components::IKChainConfig>& chains);

    private:
        static std::streampos findIKChainOffset(const std::string& meshPath);
        static bool readFilePrefix(const std::string& meshPath, std::streampos offset,
                                   std::vector<char>& outData);
        static bool writeIKChainFile(const std::string& meshPath, const std::vector<char>& prefixData,
                                     const std::vector<components::IKChainConfig>& chains);
    };
}
