// VK-1657: the sequence-editor preview maps the new override parameters like the scene renderer
// does — blendMode (clamped to a VFXBlendMode value) and colorTint (start colour and every
// ColorOverLifetime stop; ColorBySpeed and the ribbon tail stay authored). sortOrder has no
// preview field. The tint multiplies in place, so callers rebuild the params before each call.

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <impl/vfx/VFXPreviewOverrideBridge.hpp>

#include <variant>

namespace
{
    services::VFXPreviewParams paramsWithGradients()
    {
        services::VFXPreviewParams params;
        params.startColor = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);

        vfx::ColorOverLifetimeConfig colorOverLifetime;
        colorOverLifetime.gradient.stops = {
            vfx::VFXGradientStop{0.0f, glm::vec4(1.0f, 0.8f, 0.4f, 1.0f)},
            vfx::VFXGradientStop{1.0f, glm::vec4(0.2f, 0.2f, 0.2f, 0.0f)}
        };
        params.modifiers.modifiers.push_back(colorOverLifetime);

        vfx::ColorBySpeedConfig colorBySpeed;
        params.modifiers.modifiers.push_back(colorBySpeed);
        return params;
    }

    const vfx::ColorOverLifetimeConfig& colorOverLifetimeOf(const services::VFXPreviewParams& params)
    {
        return std::get<vfx::ColorOverLifetimeConfig>(params.modifiers.modifiers[0]);
    }

    const vfx::ColorBySpeedConfig& colorBySpeedOf(const services::VFXPreviewParams& params)
    {
        return std::get<vfx::ColorBySpeedConfig>(params.modifiers.modifiers[1]);
    }
}

TEST_SUITE("VFXPreviewOverrideBridge")
{
    TEST_CASE("blendMode maps to the preview blend mode, clamped to a VFXBlendMode value")
    {
        for (int mode = 0; mode < vfx::kBlendModeCount; ++mode)
        {
            services::VFXPreviewParams params;
            services::VFXEmitterOverrides overrides;
            overrides.blendMode = mode;
            services::applyToPreviewParams(params, overrides);
            CHECK(static_cast<int>(params.blendMode) == mode);
        }

        services::VFXPreviewParams low;
        services::VFXEmitterOverrides belowRange;
        belowRange.blendMode = -1;
        services::applyToPreviewParams(low, belowRange);
        CHECK(low.blendMode == vfx::VFXBlendMode::Alpha);

        services::VFXPreviewParams high;
        services::VFXEmitterOverrides aboveRange;
        aboveRange.blendMode = 7;
        services::applyToPreviewParams(high, aboveRange);
        CHECK(high.blendMode == vfx::VFXBlendMode::Multiply);
    }

    TEST_CASE("colorTint multiplies the start colour and every ColorOverLifetime stop only")
    {
        services::VFXPreviewParams params = paramsWithGradients();
        const vfx::VFXGradient speedGradientBefore = colorBySpeedOf(params).gradient;
        const glm::vec4 tint(1.0f, 0.5f, 0.0f, 0.5f);

        services::VFXEmitterOverrides overrides;
        overrides.colorTint = tint;
        services::applyToPreviewParams(params, overrides);

        CHECK(params.startColor.x == doctest::Approx(1.0f));
        CHECK(params.startColor.y == doctest::Approx(0.25f));
        CHECK(params.startColor.z == doctest::Approx(0.0f));
        CHECK(params.startColor.w == doctest::Approx(0.5f));

        const auto& stops = colorOverLifetimeOf(params).gradient.stops;
        REQUIRE(stops.size() == 2);
        CHECK(stops[0].color.y == doctest::Approx(0.4f));
        CHECK(stops[0].color.z == doctest::Approx(0.0f));
        CHECK(stops[1].color.x == doctest::Approx(0.2f));
        CHECK(stops[1].color.w == doctest::Approx(0.0f));
        CHECK(stops[0].position == doctest::Approx(0.0f)); // positions untouched
        CHECK(stops[1].position == doctest::Approx(1.0f));

        const auto& speedStops = colorBySpeedOf(params).gradient.stops;
        REQUIRE(speedStops.size() == speedGradientBefore.stops.size());
        for (size_t i = 0; i < speedStops.size(); ++i)
        {
            CHECK(speedStops[i].color == speedGradientBefore.stops[i].color);
        }
    }

    TEST_CASE("the tint applies after a startColor override and ignores negative components")
    {
        services::VFXPreviewParams params;
        services::VFXEmitterOverrides overrides;
        overrides.startColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        overrides.colorTint = glm::vec4(2.0f, -1.0f, 1.0f, 1.0f);
        services::applyToPreviewParams(params, overrides);

        CHECK(params.startColor.x == doctest::Approx(1.0f));
        CHECK(params.startColor.y == doctest::Approx(0.0f)); // a negative component counts as 0
        CHECK(params.startColor.z == doctest::Approx(0.5f));
    }

    TEST_CASE("an empty ColorOverLifetime gradient takes the tint like the runtime LUT")
    {
        services::VFXPreviewParams params;
        vfx::ColorOverLifetimeConfig colorOverLifetime;
        colorOverLifetime.gradient.stops.clear();
        params.modifiers.modifiers.push_back(colorOverLifetime);

        services::VFXEmitterOverrides overrides;
        overrides.colorTint = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
        services::applyToPreviewParams(params, overrides);

        const auto& stops = colorOverLifetimeOf(params).gradient.stops;
        REQUIRE(stops.size() == 1);
        CHECK(stops[0].color.y == doctest::Approx(0.5f));
        CHECK(stops[0].color.z == doctest::Approx(0.25f));
    }

    TEST_CASE("sortOrder has no preview field and leaves the params unchanged")
    {
        services::VFXPreviewParams params = paramsWithGradients();
        const services::VFXPreviewParams before = params;

        services::VFXEmitterOverrides overrides;
        overrides.sortOrder = 12;
        services::applyToPreviewParams(params, overrides);

        CHECK(params.startColor == before.startColor);
        CHECK(params.blendMode == before.blendMode);
        CHECK(colorOverLifetimeOf(params).gradient.stops.size() == colorOverLifetimeOf(before).gradient.stops.size());
        CHECK(colorOverLifetimeOf(params).gradient.stops[0].color == colorOverLifetimeOf(before).gradient.stops[0].color);
    }
}
