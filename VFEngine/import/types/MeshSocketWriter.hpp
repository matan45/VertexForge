#pragma once

#include "animator/SocketTypes.hpp"
#include <string>
#include <vector>

namespace types
{
    class MeshSocketWriter
    {
    public:
        // Rewrites socket data in an existing .vfMesh file.
        // Preserves all other data (LODs, meshlets, convex hulls, skeleton bones).
        // Returns true on success.
        static bool saveSocketsToMesh(const std::string& meshPath,
                                       const std::vector<animator::SocketDefinition>& sockets);

    private:
        // Finds the file offset where socket data should be written
        // (right after globalInverseTransform in the skeleton section).
        // Returns 0 on failure.
        static std::streampos findSocketDataOffset(const std::string& meshPath);
    };
}
