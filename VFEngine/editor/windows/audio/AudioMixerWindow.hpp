#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "types/AudioEffectTypes.hpp"

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
        void drawDuckingSection();
        void drawEffectChainSection();
        void drawReverbEditor(const std::string& busName, const types::BusEffectConfig& effect);
        void drawEQEditor(const std::string& busName, const types::BusEffectConfig& effect);
        void drawCompressorEditor(const std::string& busName, const types::BusEffectConfig& effect);
        void drawEchoEditor(const std::string& busName, const types::BusEffectConfig& effect);
        void drawChorusEditor(const std::string& busName, const types::BusEffectConfig& effect);
    };
}
