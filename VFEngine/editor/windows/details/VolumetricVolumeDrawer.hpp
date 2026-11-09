#pragma once
#include "data/EntityHandle.hpp"
#include <cstdint>
#include <string>

namespace windows::details
{
    class VolumetricVolumeDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        // Bake / Cancel / Clear and the outcome of the bake this drawer started (VK-1660): it is polled every
        // frame until it finishes, then saved to a .vfVolNav.
        void drawBakeControls(services::EntityHandle handle, bool playMode);
        void showResult(services::EntityHandle volume, std::string message, bool isError);

        uint64_t bakeTicket = 0; // the running bake (0 = none)
        services::EntityHandle bakeVolume;
        // The last bake / save outcome, shown on its volume until the next bake.
        std::string resultMessage;
        bool resultIsError = false;
        services::EntityHandle resultVolume;
    };
}
