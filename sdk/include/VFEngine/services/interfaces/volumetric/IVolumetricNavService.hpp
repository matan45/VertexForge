#pragma once

namespace services
{
    class IVolumetricNavService
    {
    public:
        virtual ~IVolumetricNavService() = default;

        virtual void registerEventHandlers() = 0;

        // The "Volumetric" frame task, every frame (VK-1660): harvests finished bakes in every mode, moves the
        // agents only while `simulate` (Play with game time running) and draws the debug overlay.
        virtual void update(float deltaTime, bool simulate) = 0;
    };
}
