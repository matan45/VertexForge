#include "ReverbPresets.hpp"
#include <AL/efx-presets.h>
#include <unordered_map>
#include <algorithm>

namespace core::audio
{
    namespace
    {
        types::ReverbParams fromEfx(const EFXEAXREVERBPROPERTIES& efx)
        {
            types::ReverbParams p;
            p.density = efx.flDensity;
            p.diffusion = efx.flDiffusion;
            p.gain = efx.flGain;
            p.gainHF = efx.flGainHF;
            p.gainLF = efx.flGainLF;
            p.decayTime = efx.flDecayTime;
            p.decayHFRatio = efx.flDecayHFRatio;
            p.decayLFRatio = efx.flDecayLFRatio;
            p.reflectionsGain = efx.flReflectionsGain;
            p.reflectionsDelay = efx.flReflectionsDelay;
            p.reflectionsPan[0] = efx.flReflectionsPan[0];
            p.reflectionsPan[1] = efx.flReflectionsPan[1];
            p.reflectionsPan[2] = efx.flReflectionsPan[2];
            p.lateReverbGain = efx.flLateReverbGain;
            p.lateReverbDelay = efx.flLateReverbDelay;
            p.lateReverbPan[0] = efx.flLateReverbPan[0];
            p.lateReverbPan[1] = efx.flLateReverbPan[1];
            p.lateReverbPan[2] = efx.flLateReverbPan[2];
            p.echoTime = efx.flEchoTime;
            p.echoDepth = efx.flEchoDepth;
            p.modulationTime = efx.flModulationTime;
            p.modulationDepth = efx.flModulationDepth;
            p.airAbsorptionGainHF = efx.flAirAbsorptionGainHF;
            p.hfReference = efx.flHFReference;
            p.lfReference = efx.flLFReference;
            p.roomRolloffFactor = efx.flRoomRolloffFactor;
            p.decayHFLimit = efx.iDecayHFLimit;
            return p;
        }

        struct PresetEntry
        {
            const char* name;
            EFXEAXREVERBPROPERTIES efx;
        };

        // clang-format off
        static const PresetEntry s_presets[] = {
            {"Generic",            EFX_REVERB_PRESET_GENERIC},
            {"Padded Cell",        EFX_REVERB_PRESET_PADDEDCELL},
            {"Room",               EFX_REVERB_PRESET_ROOM},
            {"Bathroom",           EFX_REVERB_PRESET_BATHROOM},
            {"Living Room",        EFX_REVERB_PRESET_LIVINGROOM},
            {"Stone Room",         EFX_REVERB_PRESET_STONEROOM},
            {"Auditorium",         EFX_REVERB_PRESET_AUDITORIUM},
            {"Concert Hall",       EFX_REVERB_PRESET_CONCERTHALL},
            {"Cave",               EFX_REVERB_PRESET_CAVE},
            {"Arena",              EFX_REVERB_PRESET_ARENA},
            {"Hangar",             EFX_REVERB_PRESET_HANGAR},
            {"Carpeted Hallway",   EFX_REVERB_PRESET_CARPETEDHALLWAY},
            {"Hallway",            EFX_REVERB_PRESET_HALLWAY},
            {"Stone Corridor",     EFX_REVERB_PRESET_STONECORRIDOR},
            {"Alley",              EFX_REVERB_PRESET_ALLEY},
            {"Forest",             EFX_REVERB_PRESET_FOREST},
            {"City",               EFX_REVERB_PRESET_CITY},
            {"Mountains",          EFX_REVERB_PRESET_MOUNTAINS},
            {"Quarry",             EFX_REVERB_PRESET_QUARRY},
            {"Plain",              EFX_REVERB_PRESET_PLAIN},
            {"Parking Lot",        EFX_REVERB_PRESET_PARKINGLOT},
            {"Sewer Pipe",         EFX_REVERB_PRESET_SEWERPIPE},
            {"Underwater",         EFX_REVERB_PRESET_UNDERWATER},
            {"Drugged",            EFX_REVERB_PRESET_DRUGGED},
            {"Dizzy",              EFX_REVERB_PRESET_DIZZY},
            {"Psychotic",          EFX_REVERB_PRESET_PSYCHOTIC},
            {"Castle Small Room",  EFX_REVERB_PRESET_CASTLE_SMALLROOM},
            {"Castle Short Passage", EFX_REVERB_PRESET_CASTLE_SHORTPASSAGE},
            {"Castle Medium Room", EFX_REVERB_PRESET_CASTLE_MEDIUMROOM},
            {"Castle Large Room",  EFX_REVERB_PRESET_CASTLE_LARGEROOM},
            {"Castle Long Passage", EFX_REVERB_PRESET_CASTLE_LONGPASSAGE},
            {"Castle Hall",        EFX_REVERB_PRESET_CASTLE_HALL},
            {"Castle Cupboard",    EFX_REVERB_PRESET_CASTLE_CUPBOARD},
            {"Castle Courtyard",   EFX_REVERB_PRESET_CASTLE_COURTYARD},
            {"Castle Alcove",      EFX_REVERB_PRESET_CASTLE_ALCOVE},
            {"Factory Small Room", EFX_REVERB_PRESET_FACTORY_SMALLROOM},
            {"Factory Short Passage", EFX_REVERB_PRESET_FACTORY_SHORTPASSAGE},
            {"Factory Medium Room", EFX_REVERB_PRESET_FACTORY_MEDIUMROOM},
            {"Factory Large Room", EFX_REVERB_PRESET_FACTORY_LARGEROOM},
            {"Factory Long Passage", EFX_REVERB_PRESET_FACTORY_LONGPASSAGE},
            {"Factory Hall",       EFX_REVERB_PRESET_FACTORY_HALL},
            {"Factory Cupboard",   EFX_REVERB_PRESET_FACTORY_CUPBOARD},
            {"Factory Courtyard",  EFX_REVERB_PRESET_FACTORY_COURTYARD},
            {"Factory Alcove",     EFX_REVERB_PRESET_FACTORY_ALCOVE},
            {"Ice Palace Small Room", EFX_REVERB_PRESET_ICEPALACE_SMALLROOM},
            {"Ice Palace Short Passage", EFX_REVERB_PRESET_ICEPALACE_SHORTPASSAGE},
            {"Ice Palace Medium Room", EFX_REVERB_PRESET_ICEPALACE_MEDIUMROOM},
            {"Ice Palace Large Room", EFX_REVERB_PRESET_ICEPALACE_LARGEROOM},
            {"Ice Palace Long Passage", EFX_REVERB_PRESET_ICEPALACE_LONGPASSAGE},
            {"Ice Palace Hall",    EFX_REVERB_PRESET_ICEPALACE_HALL},
            {"Ice Palace Cupboard", EFX_REVERB_PRESET_ICEPALACE_CUPBOARD},
            {"Ice Palace Courtyard", EFX_REVERB_PRESET_ICEPALACE_COURTYARD},
            {"Ice Palace Alcove",  EFX_REVERB_PRESET_ICEPALACE_ALCOVE},
            {"Space Station Small Room", EFX_REVERB_PRESET_SPACESTATION_SMALLROOM},
            {"Space Station Short Passage", EFX_REVERB_PRESET_SPACESTATION_SHORTPASSAGE},
            {"Space Station Medium Room", EFX_REVERB_PRESET_SPACESTATION_MEDIUMROOM},
            {"Space Station Large Room", EFX_REVERB_PRESET_SPACESTATION_LARGEROOM},
            {"Space Station Long Passage", EFX_REVERB_PRESET_SPACESTATION_LONGPASSAGE},
            {"Space Station Hall", EFX_REVERB_PRESET_SPACESTATION_HALL},
            {"Space Station Cupboard", EFX_REVERB_PRESET_SPACESTATION_CUPBOARD},
            {"Space Station Alcove", EFX_REVERB_PRESET_SPACESTATION_ALCOVE},
            {"Wooden Small Room",  EFX_REVERB_PRESET_WOODEN_SMALLROOM},
            {"Wooden Short Passage", EFX_REVERB_PRESET_WOODEN_SHORTPASSAGE},
            {"Wooden Medium Room", EFX_REVERB_PRESET_WOODEN_MEDIUMROOM},
            {"Wooden Large Room",  EFX_REVERB_PRESET_WOODEN_LARGEROOM},
            {"Wooden Long Passage", EFX_REVERB_PRESET_WOODEN_LONGPASSAGE},
            {"Wooden Hall",        EFX_REVERB_PRESET_WOODEN_HALL},
            {"Wooden Cupboard",    EFX_REVERB_PRESET_WOODEN_CUPBOARD},
            {"Wooden Courtyard",   EFX_REVERB_PRESET_WOODEN_COURTYARD},
            {"Wooden Alcove",      EFX_REVERB_PRESET_WOODEN_ALCOVE},
            {"Sport Empty Stadium", EFX_REVERB_PRESET_SPORT_EMPTYSTADIUM},
            {"Sport Squash Court", EFX_REVERB_PRESET_SPORT_SQUASHCOURT},
            {"Sport Small Swimming Pool", EFX_REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL},
            {"Sport Large Swimming Pool", EFX_REVERB_PRESET_SPORT_LARGESWIMMINGPOOL},
            {"Sport Gymnasium",    EFX_REVERB_PRESET_SPORT_GYMNASIUM},
            {"Sport Full Stadium", EFX_REVERB_PRESET_SPORT_FULLSTADIUM},
            {"Sport Stadium Tannoy", EFX_REVERB_PRESET_SPORT_STADIUMTANNOY},
            {"Prefab Workshop",    EFX_REVERB_PRESET_PREFAB_WORKSHOP},
            {"Prefab School Room", EFX_REVERB_PRESET_PREFAB_SCHOOLROOM},
            {"Prefab Practise Room", EFX_REVERB_PRESET_PREFAB_PRACTISEROOM},
            {"Prefab Outhouse",    EFX_REVERB_PRESET_PREFAB_OUTHOUSE},
            {"Prefab Caravan",     EFX_REVERB_PRESET_PREFAB_CARAVAN},
            {"Dome Tomb",          EFX_REVERB_PRESET_DOME_TOMB},
            {"Pipe Small",         EFX_REVERB_PRESET_PIPE_SMALL},
            {"Dome Saint Pauls",   EFX_REVERB_PRESET_DOME_SAINTPAULS},
            {"Pipe Long Thin",     EFX_REVERB_PRESET_PIPE_LONGTHIN},
            {"Pipe Large",         EFX_REVERB_PRESET_PIPE_LARGE},
            {"Pipe Resonant",      EFX_REVERB_PRESET_PIPE_RESONANT},
            {"Outdoors Backyard",  EFX_REVERB_PRESET_OUTDOORS_BACKYARD},
            {"Outdoors Rolling Plains", EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS},
            {"Outdoors Deep Canyon", EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON},
            {"Outdoors Creek",     EFX_REVERB_PRESET_OUTDOORS_CREEK},
            {"Outdoors Valley",    EFX_REVERB_PRESET_OUTDOORS_VALLEY},
            {"Mood Heaven",        EFX_REVERB_PRESET_MOOD_HEAVEN},
            {"Mood Hell",          EFX_REVERB_PRESET_MOOD_HELL},
            {"Mood Memory",        EFX_REVERB_PRESET_MOOD_MEMORY},
            {"Driving Commentator", EFX_REVERB_PRESET_DRIVING_COMMENTATOR},
            {"Driving Pit Garage", EFX_REVERB_PRESET_DRIVING_PITGARAGE},
            {"Driving In-Car Racer", EFX_REVERB_PRESET_DRIVING_INCAR_RACER},
            {"Driving In-Car Sports", EFX_REVERB_PRESET_DRIVING_INCAR_SPORTS},
            {"Driving In-Car Luxury", EFX_REVERB_PRESET_DRIVING_INCAR_LUXURY},
            {"Driving Full Grand Prix", EFX_REVERB_PRESET_DRIVING_FULLGRANDSTAND},
            {"Driving Empty Grand Prix", EFX_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND},
            {"Driving Tunnel",     EFX_REVERB_PRESET_DRIVING_TUNNEL},
            {"City Streets",       EFX_REVERB_PRESET_CITY_STREETS},
            {"City Subway",        EFX_REVERB_PRESET_CITY_SUBWAY},
            {"City Museum",        EFX_REVERB_PRESET_CITY_MUSEUM},
            {"City Library",       EFX_REVERB_PRESET_CITY_LIBRARY},
            {"City Underpass",     EFX_REVERB_PRESET_CITY_UNDERPASS},
            {"City Abandoned",     EFX_REVERB_PRESET_CITY_ABANDONED},
            {"Dusty Room",         EFX_REVERB_PRESET_DUSTYROOM},
            {"Chapel",             EFX_REVERB_PRESET_CHAPEL},
            {"Small Water Room",   EFX_REVERB_PRESET_SMALLWATERROOM},
        };
        // clang-format on

        static constexpr size_t s_presetCount = sizeof(s_presets) / sizeof(s_presets[0]);
    }

    types::ReverbParams ReverbPresets::fromPreset(const std::string& name)
    {
        for (size_t i = 0; i < s_presetCount; ++i)
        {
            if (name == s_presets[i].name)
            {
                auto params = fromEfx(s_presets[i].efx);
                params.presetName = name;
                return params;
            }
        }
        // Default to Generic
        auto params = fromEfx(s_presets[0].efx);
        params.presetName = "Generic";
        return params;
    }

    std::vector<std::string> ReverbPresets::getPresetNames()
    {
        std::vector<std::string> names;
        names.reserve(s_presetCount);
        for (size_t i = 0; i < s_presetCount; ++i)
        {
            names.emplace_back(s_presets[i].name);
        }
        return names;
    }
}
