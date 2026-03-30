#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace windows
{
    class AudioMixerWindow
    {
    private:
        bool visible = false;
        std::string snapshotName;
        std::string newBusName;
        int selectedParentIndex = 0;

        // Effect chain editing
        std::string selectedBusName;
        int addEffectTypeIndex = 0;

    public:
        void draw();
        void drawContent();
        void show();

    private:
        void drawBusChannels();
        void drawCreateBusSection();
        void drawSnapshotSection();
        void drawEffectChainSection();
        void drawReverbEditor(const std::string& busName, uint32_t effectId);
        void drawEQEditor(const std::string& busName, uint32_t effectId);
        void drawCompressorEditor(const std::string& busName, uint32_t effectId);
        void drawEchoEditor(const std::string& busName, uint32_t effectId);
        void drawChorusEditor(const std::string& busName, uint32_t effectId);
    };
}
