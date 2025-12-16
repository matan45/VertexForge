#pragma once
#include "IMaterialPreviewProvider.hpp"
#include "IMeshPreviewProvider.hpp"

namespace services {

    // Combined interface for backwards compatibility.
    // New code should prefer using IMaterialPreviewProvider or IMeshPreviewProvider directly.
    class IPreviewProvider : public IMaterialPreviewProvider, public IMeshPreviewProvider {
    public:
        ~IPreviewProvider() override = default;
    };

}
