#include <doctest.h>
#include <types/AudioTypes.hpp>
#include <types/AudioEffectTypes.hpp>
#include <variant>

// ============================================================
// VK-1060: Audio types unit tests
// ============================================================

TEST_SUITE("AudioTypes") {

// ---- AudioSettings::createDefault ----

TEST_CASE("AudioSettings::createDefault: masterVolume > 0") {
    auto settings = types::AudioSettings::createDefault();
    CHECK(settings.masterVolume > 0.0f);
}

TEST_CASE("AudioSettings::createDefault: speedOfSound > 0") {
    auto settings = types::AudioSettings::createDefault();
    CHECK(settings.speedOfSound > 0.0f);
}

TEST_CASE("AudioSettings::createDefault: busDefinitions empty by default") {
    // Note: createDefault() does not populate busDefinitions
    // The bus definitions are added by the audio system at runtime
    auto settings = types::AudioSettings::createDefault();
    // Default factory does not add bus definitions;
    // this verifies the vector is initialized (empty)
    CHECK(settings.busDefinitions.empty());
}

// ---- BusEffectConfig::createDefault ----

TEST_CASE("BusEffectConfig::createDefault(Reverb): type is Reverb and enabled") {
    auto config = types::BusEffectConfig::createDefault(types::AudioEffectType::Reverb);
    CHECK(config.type == types::AudioEffectType::Reverb);
    CHECK(config.enabled == true);
    CHECK(std::holds_alternative<types::ReverbParams>(config.params));
}

TEST_CASE("BusEffectConfig::createDefault(EQ): type is EQ") {
    auto config = types::BusEffectConfig::createDefault(types::AudioEffectType::EQ);
    CHECK(config.type == types::AudioEffectType::EQ);
    CHECK(std::holds_alternative<types::EQParams>(config.params));
}

TEST_CASE("BusEffectConfig::createDefault(Compressor): type is Compressor") {
    auto config = types::BusEffectConfig::createDefault(types::AudioEffectType::Compressor);
    CHECK(config.type == types::AudioEffectType::Compressor);
    CHECK(std::holds_alternative<types::CompressorParams>(config.params));
}

TEST_CASE("BusEffectConfig::createDefault(Echo): type is Echo") {
    auto config = types::BusEffectConfig::createDefault(types::AudioEffectType::Echo);
    CHECK(config.type == types::AudioEffectType::Echo);
    CHECK(std::holds_alternative<types::EchoParams>(config.params));
}

TEST_CASE("BusEffectConfig::createDefault(Chorus): type is Chorus") {
    auto config = types::BusEffectConfig::createDefault(types::AudioEffectType::Chorus);
    CHECK(config.type == types::AudioEffectType::Chorus);
    CHECK(std::holds_alternative<types::ChorusParams>(config.params));
}

TEST_CASE("BusEffectConfig::createDefault: all effect types produce enabled config") {
    auto allTypes = {
        types::AudioEffectType::Reverb,
        types::AudioEffectType::EQ,
        types::AudioEffectType::Compressor,
        types::AudioEffectType::Echo,
        types::AudioEffectType::Chorus
    };
    for (auto effectType : allTypes) {
        auto config = types::BusEffectConfig::createDefault(effectType);
        CHECK(config.type == effectType);
        CHECK(config.enabled == true);
    }
}

// ---- audioEffectTypeToString / stringToAudioEffectType roundtrip ----

TEST_CASE("audioEffectTypeToString and stringToAudioEffectType roundtrip") {
    auto allTypes = {
        types::AudioEffectType::Reverb,
        types::AudioEffectType::EQ,
        types::AudioEffectType::Compressor,
        types::AudioEffectType::Echo,
        types::AudioEffectType::Chorus
    };
    for (auto effectType : allTypes) {
        std::string str = types::audioEffectTypeToString(effectType);
        CHECK_FALSE(str.empty());
        CHECK(str != "Unknown");
        types::AudioEffectType roundtripped = types::stringToAudioEffectType(str);
        CHECK(roundtripped == effectType);
    }
}

// ---- ReverbParams defaults ----

TEST_CASE("ReverbParams: decayTime > 0") {
    types::ReverbParams params;
    CHECK(params.decayTime > 0.0f);
}

TEST_CASE("ReverbParams: density in [0,1]") {
    types::ReverbParams params;
    CHECK(params.density >= 0.0f);
    CHECK(params.density <= 1.0f);
}

// ---- EQParams defaults ----

TEST_CASE("EQParams: cutoff frequencies positive") {
    types::EQParams params;
    CHECK(params.lowCutoff > 0.0f);
    CHECK(params.highCutoff > 0.0f);
    CHECK(params.mid1Center > 0.0f);
    CHECK(params.mid2Center > 0.0f);
}

} // TEST_SUITE
