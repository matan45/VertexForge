#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class IKDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        void refreshBoneNames(services::EntityHandle handle);

        // State for adding new chains
        char newChainName[64] = "NewChain";

        // Cached bone names from entity's skeleton
        std::vector<std::string> boneNames;
        std::vector<int32_t> boneParentIndices;
        services::EntityHandle cachedEntity{};
    };
}
