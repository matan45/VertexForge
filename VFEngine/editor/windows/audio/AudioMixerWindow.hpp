#pragma once

#include <string>
#include <vector>

namespace windows
{
    class AudioMixerWindow
    {
    private:
        bool visible = false;
        std::string snapshotName;
        std::string newBusName;
        int selectedParentIndex = 0;

    public:
        void draw();
        void show();

    private:
        void drawBusChannels();
        void drawCreateBusSection();
        void drawSnapshotSection();
    };
}
