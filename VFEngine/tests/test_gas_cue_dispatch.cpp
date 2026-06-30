#include <doctest.h>
#include "../../plugins/GameplayAbilitySystem/gas/core/CueDispatch.hpp"
#include "../../plugins/GameplayAbilitySystem/gas/core/ActivationPipeline.hpp"
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// VK-816 Phase 5 — engine-free runtime pieces that can be unit
// tested without the plugin DLL: the cue dispatcher (resolve +
// emit to a sink) and the activation-status code mapping the
// _gas_* natives + GameplayAbilitySystem.mt facade rely on.
// ============================================================

TEST_CASE("gas::dispatchCues resolves cue ids and emits to the sink")
{
    std::unordered_map<std::string, gas::GameplayCueSpec> table;
    {
        gas::GameplayCueSpec hit;
        hit.id = "Hit";
        hit.trigger = gas::CueTrigger::OnExecute;
        hit.vfxPath = "vfx/hit.vfVFX";
        hit.audioPath = "audio/hit.vfAudio";
        table["Hit"] = hit;

        gas::GameplayCueSpec buff;
        buff.id = "Buff";
        buff.trigger = gas::CueTrigger::OnApply;
        buff.vfxPath = "vfx/buff.vfVFX";
        table["Buff"] = buff;
    }

    gas::CueResolver resolve = [&table](const std::string& id) -> const gas::GameplayCueSpec*
    {
        auto it = table.find(id);
        return it == table.end() ? nullptr : &it->second;
    };

    std::vector<gas::CueEvent> emitted;
    gas::CueSink sink = [&emitted](const gas::CueEvent& ev) { emitted.push_back(ev); };

    SUBCASE("known ids dispatch with position + payload; unknown ids skipped")
    {
        const int count = gas::dispatchCues({"Hit", "Buff", "DoesNotExist"}, resolve, sink, 1.0f, 2.0f, 3.0f);
        CHECK(count == 2);
        REQUIRE(emitted.size() == 2);
        CHECK(emitted[0].cueId == "Hit");
        CHECK(emitted[0].vfxPath == "vfx/hit.vfVFX");
        CHECK(emitted[0].audioPath == "audio/hit.vfAudio");
        CHECK(emitted[0].trigger == gas::CueTrigger::OnExecute);
        CHECK(emitted[0].x == doctest::Approx(1.0f));
        CHECK(emitted[0].y == doctest::Approx(2.0f));
        CHECK(emitted[0].z == doctest::Approx(3.0f));
        CHECK(emitted[1].cueId == "Buff");
        CHECK(emitted[1].audioPath.empty());
    }

    SUBCASE("empty cue list dispatches nothing")
    {
        CHECK(gas::dispatchCues({}, resolve, sink, 0, 0, 0) == 0);
        CHECK(emitted.empty());
    }

    SUBCASE("empty ids are skipped")
    {
        CHECK(gas::dispatchCues({"", "Hit"}, resolve, sink, 0, 0, 0) == 1);
    }
}

TEST_CASE("gas::ActivationStatus codes match the documented facade values")
{
    // GameplayAbilitySystem.mt documents these numeric codes; the _gas_* natives
    // return static_cast<int>(status). Guard against enum reordering.
    CHECK(static_cast<int>(gas::ActivationStatus::Success) == 0);
    CHECK(static_cast<int>(gas::ActivationStatus::InvalidEntity) == 1);
    CHECK(static_cast<int>(gas::ActivationStatus::NotGranted) == 2);
    CHECK(static_cast<int>(gas::ActivationStatus::BlockedByTags) == 3);
    CHECK(static_cast<int>(gas::ActivationStatus::MissingTags) == 4);
    CHECK(static_cast<int>(gas::ActivationStatus::OnCooldown) == 5);
    CHECK(static_cast<int>(gas::ActivationStatus::CostNotMet) == 6);
    CHECK(static_cast<int>(gas::ActivationStatus::NoTarget) == 7);
    CHECK(static_cast<int>(gas::ActivationStatus::Internal) == 8);
}
