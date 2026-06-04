#pragma once
#include "ActionMappingTypes.hpp"
#include <string>
#include <vector>

namespace services
{
    struct EditorActionInfo
    {
        std::string name;
        std::string category;
        std::string displayName;
        std::vector<InputBinding> currentBindings;
        std::vector<InputBinding> defaultBindings;
    };

    struct KeybindingConflict
    {
        std::string conflictingAction;
        InputBinding binding;
    };
}
