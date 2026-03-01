#pragma once
#include "data/EntityHandle.hpp"
#include <string>

namespace windows::details
{
    class IKDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        // State for adding new chains
        char newChainName[64] = "NewChain";
        char newTipBone[128] = "";
    };
}
