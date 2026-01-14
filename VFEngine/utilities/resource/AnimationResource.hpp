#pragma once
#include <string>
#include <string_view>
#include "Types.hpp"

namespace resource
{
    class AnimationResource
    {
    public:
        // Load animation from .vfAnim file
        static AnimationData loadAnimation(std::string_view path);

        // Validate animation file header
        static bool validateFile(std::string_view path);

    private:
        static std::string readString(std::ifstream& file);
    };
}
