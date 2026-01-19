#pragma once
#include <string_view>
#include "Types.hpp"

namespace resource
{
    class AnimationResource
    {
    public:
        static AnimationData loadAnimation(std::string_view path);

    private:
        static std::string readString(std::ifstream& file);
    };
}
