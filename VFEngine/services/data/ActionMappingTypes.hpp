#pragma once
#include <string>
#include <vector>

namespace services {

    enum class BindingType : int {
        Key = 0,
        MouseButton = 1
    };

    struct InputBinding {
        BindingType type;
        int code;
        bool requireShift = false;
        bool requireCtrl = false;
        bool requireAlt = false;

        bool operator==(const InputBinding& other) const {
            return type == other.type && code == other.code
                && requireShift == other.requireShift
                && requireCtrl == other.requireCtrl
                && requireAlt == other.requireAlt;
        }

        bool operator!=(const InputBinding& other) const {
            return !(*this == other);
        }
    };

    struct ActionDefinition {
        std::string name;
        std::vector<InputBinding> bindings;
    };

}
