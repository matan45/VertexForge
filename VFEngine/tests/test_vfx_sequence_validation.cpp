#include <doctest.h>

#include <vfx/VFXOverrideNames.hpp>
#include <vfx/VFXParameterRegistry.hpp>
#include <vfx/VFXSequenceValidation.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>

#include <string>
#include <unordered_set>

namespace
{
    bool hasDiagnostic(const vfx::validation::ValidationReport& report,
                       vfx::validation::Severity severity,
                       const std::string& text)
    {
        for (const auto& diagnostic : report.diagnostics)
        {
            if (diagnostic.severity == severity &&
                diagnostic.message.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    vfx::VFXSequenceStep makeValidStep()
    {
        vfx::VFXSequenceStep step;
        step.label = "Valid";
        step.vfxRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xABC001));
        return step;
    }
}

TEST_SUITE("VFXSequenceValidation")
{
    TEST_CASE("override name table stays in sync with phase-one runtime contract")
    {
        CHECK(vfx::overridenames::scalarOverrides.size() == 13);
        CHECK(vfx::overridenames::vectorOverrides.size() == 5);
        CHECK(vfx::overridenames::isScalarOverride("spawnRate"));
        CHECK(vfx::overridenames::isScalarOverride("collisionEnabled"));
        CHECK(vfx::overridenames::isVectorOverride("emitDirection"));
        CHECK(vfx::overridenames::isVectorOverride("startColor"));
        CHECK(vfx::kExposedParameters.size() == 18);
    }

    TEST_CASE("missing and optionally unresolved refs are reported")
    {
        asset::AssetDatabase::instance().clear();

        vfx::VFXSequenceData data;
        data.steps.push_back(vfx::VFXSequenceStep{});

        auto report = vfx::validation::validateSequence(data);
        CHECK(report.hasErrors());
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Error, "missing .vfVFX reference"));

        data.steps.clear();
        data.steps.push_back(makeValidStep());

        vfx::validation::ValidationContext context;
        context.checkRefResolvable = true;
        report = vfx::validation::validateSequence(data, context);
        CHECK_FALSE(report.hasErrors());
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "does not currently resolve"));
    }

    TEST_CASE("override diagnostics catch unknown, type mismatch and duplicate names")
    {
        vfx::VFXSequenceData data;
        auto step = makeValidStep();
        step.overrides.push_back(vfx::VFXParamOverride{"startColor", 1.0f});
        step.overrides.push_back(vfx::VFXParamOverride{"notARealOverride", 2.0f});
        step.overrides.push_back(vfx::VFXParamOverride{"spawnRate", 3.0f});
        step.overrides.push_back(vfx::VFXParamOverride{"spawnRate", 4.0f});
        data.steps.push_back(step);

        const auto report = vfx::validation::validateSequence(data);
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "expects Color"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "unknown override"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Info, "duplicate override"));
    }

    TEST_CASE("timing cue and socket rules are lenient but visible")
    {
        vfx::VFXSequenceData data;

        auto impact = makeValidStep();
        impact.label = "ImpactA";
        impact.cueName = "impact";
        impact.startTime = -0.5f;
        impact.socketName = " muzzle ";
        impact.duration = -1.0f;
        data.steps.push_back(impact);

        auto duplicate = makeValidStep();
        duplicate.label = "ImpactB";
        duplicate.cueName = "impact";
        data.steps.push_back(duplicate);

        auto nearDuplicate = makeValidStep();
        nearDuplicate.label = "ImpactC";
        nearDuplicate.cueName = " Impact ";
        data.steps.push_back(nearDuplicate);

        auto emptyCue = makeValidStep();
        emptyCue.label = "EmptyCue";
        emptyCue.cueName = "";
        data.steps.push_back(emptyCue);
        data.eventMarkers.push_back(vfx::VFXSequenceEventMarker{0.25f, "signalOnly"});

        std::unordered_set<std::string> sockets{"hand_r"};
        vfx::validation::ValidationContext context;
        context.knownSocketNames = &sockets;

        const auto report = vfx::validation::validateSequence(data, context);
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "negative start time"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "negative duration"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Info, "start time is ignored"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "leading or trailing whitespace"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "not on the reference mesh"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Info, "fans out"));
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "case or whitespace"));
        CHECK_FALSE(hasDiagnostic(report, vfx::validation::Severity::Warning, "EmptyCue"));
        CHECK_FALSE(hasDiagnostic(report, vfx::validation::Severity::Info, "matches no cue-driven step"));
    }

    TEST_CASE("VK-1524 — StepOutput binding diagnostics")
    {
        using Severity = vfx::validation::Severity;

        SUBCASE("a valid source -> receiver binding produces no errors")
        {
            vfx::VFXSequenceData data;
            auto src = makeValidStep();
            src.label = "src";
            src.outputEventName = "impact";
            src.outputEventType = vfx::VFXEventType::OnCollision;
            data.steps.push_back(src);

            auto recv = makeValidStep();
            recv.label = "recv";
            recv.trigger = vfx::VFXStepTrigger::StepOutput;
            recv.sourceStepIndex = 0;
            recv.sourceEventName = "impact";
            data.steps.push_back(recv);

            const auto report = vfx::validation::validateSequence(data);
            CHECK_FALSE(report.hasErrors());
        }

        SUBCASE("missing source, empty name, self-dep, non-VFX source, name mismatch, dual-role")
        {
            vfx::VFXSequenceData data;

            auto missing = makeValidStep(); // 0
            missing.trigger = vfx::VFXStepTrigger::StepOutput;
            missing.sourceStepIndex = 99;
            missing.sourceEventName = "";
            data.steps.push_back(missing);

            auto self = makeValidStep(); // 1
            self.trigger = vfx::VFXStepTrigger::StepOutput;
            self.sourceStepIndex = 1;
            self.sourceEventName = "x";
            data.steps.push_back(self);

            vfx::VFXSequenceStep sound; // 2 — non-VFX source
            sound.kind = vfx::VFXStepKind::Sound;
            sound.label = "sound-src";
            sound.audioRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xABC002));
            sound.outputEventName = "boom";
            data.steps.push_back(sound);

            auto badSrc = makeValidStep(); // 3 — receiver of the non-VFX source
            badSrc.trigger = vfx::VFXStepTrigger::StepOutput;
            badSrc.sourceStepIndex = 2;
            badSrc.sourceEventName = "boom";
            data.steps.push_back(badSrc);

            auto realSrc = makeValidStep(); // 4 — VFX source publishing "impact"
            realSrc.outputEventName = "impact";
            data.steps.push_back(realSrc);

            auto mismatch = makeValidStep(); // 5 — binds a name the source doesn't publish
            mismatch.trigger = vfx::VFXStepTrigger::StepOutput;
            mismatch.sourceStepIndex = 4;
            mismatch.sourceEventName = "wrong";
            data.steps.push_back(mismatch);

            auto dual = makeValidStep(); // 6 — both a source and a StepOutput receiver
            dual.trigger = vfx::VFXStepTrigger::StepOutput;
            dual.sourceStepIndex = 4;
            dual.sourceEventName = "impact";
            dual.outputEventName = "chain";
            data.steps.push_back(dual);

            const auto report = vfx::validation::validateSequence(data);
            CHECK(hasDiagnostic(report, Severity::Error, "missing source step"));
            CHECK(hasDiagnostic(report, Severity::Error, "no source event name"));
            CHECK(hasDiagnostic(report, Severity::Error, "self-dependency"));
            CHECK(hasDiagnostic(report, Severity::Error, "not a VFX step"));
            CHECK(hasDiagnostic(report, Severity::Error, "does not publish an event named 'wrong'"));
            CHECK(hasDiagnostic(report, Severity::Error, "both an event source"));
        }

        SUBCASE("a socket on a StepOutput receiver warns (world-anchored)")
        {
            vfx::VFXSequenceData data;
            auto src = makeValidStep();
            src.outputEventName = "impact";
            data.steps.push_back(src);
            auto recv = makeValidStep();
            recv.trigger = vfx::VFXStepTrigger::StepOutput;
            recv.sourceStepIndex = 0;
            recv.sourceEventName = "impact";
            recv.socketName = "hand_R";
            data.steps.push_back(recv);
            const auto report = vfx::validation::validateSequence(data);
            CHECK(hasDiagnostic(report, Severity::Warning, "world-anchored"));
        }

        SUBCASE("a 2-cycle of dual-role steps is reported")
        {
            vfx::VFXSequenceData data;
            auto a = makeValidStep();
            a.outputEventName = "a";
            a.trigger = vfx::VFXStepTrigger::StepOutput;
            a.sourceStepIndex = 1;
            a.sourceEventName = "b";
            data.steps.push_back(a);
            auto b = makeValidStep();
            b.outputEventName = "b";
            b.trigger = vfx::VFXStepTrigger::StepOutput;
            b.sourceStepIndex = 0;
            b.sourceEventName = "a";
            data.steps.push_back(b);
            const auto report = vfx::validation::validateSequence(data);
            CHECK(hasDiagnostic(report, Severity::Error, "dependency cycle"));
        }
    }
}
