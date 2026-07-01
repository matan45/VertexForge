#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXSequenceTypes.hpp"
#include <vfx/VFXSequenceTypes.hpp>
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace services::events::vfxruntime
{
    struct VFXParticleEventNotification : ::events::INotification
    {
        uint32_t eventType = 0;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        uint32_t emitterIndex = 0;
        uint32_t parentInstanceId = 0;
        uint32_t entityId = 0;
        std::string vfxAssetPath;

        std::string_view getName() const override { return "VFXParticleEvent"; }
    };
}

namespace services::events::vfxsequence
{
    struct VFXComboCueFiredNotification : ::events::INotification
    {
        VFXComboInstanceId comboId = 0;
        std::string cueName;
        vfx::VFXCuePayload payload;

        std::string_view getName() const override { return "VFXComboCueFired"; }
    };
}
