#include <doctest.h>

#include <vfx/VFXOverrideNames.hpp>
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

    TEST_CASE("override diagnostics catch unknown, wrong-slot and duplicate names")
    {
        vfx::VFXSequenceData data;
        auto step = makeValidStep();
        step.scalarOverrides.emplace_back("startColor", 1.0f);
        step.scalarOverrides.emplace_back("notARealOverride", 2.0f);
        step.scalarOverrides.emplace_back("spawnRate", 3.0f);
        step.scalarOverrides.emplace_back("spawnRate", 4.0f);
        step.vectorOverrides.emplace_back("spawnRate", glm::vec4(1.0f));
        data.steps.push_back(step);

        const auto report = vfx::validation::validateSequence(data);
        CHECK(hasDiagnostic(report, vfx::validation::Severity::Warning, "wrong override list"));
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
    }
}
