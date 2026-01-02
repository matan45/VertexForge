#pragma once
#include "data/EntityHandle.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace windows::details {

    class MaterialDrawer {
    private:
        std::unordered_map<std::string, std::vector<std::string>> submeshNameCache;
    public:
        void draw(services::EntityHandle handle);
        void clearCache();

    
    };

}
