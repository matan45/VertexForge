#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <data/VFXOverrideApplier.hpp>

#include <limits>

namespace
{
    vfx::VFXPropertyValue sampleValue(vfx::VFXPropertyType type)
    {
        switch (type)
        {
        case vfx::VFXPropertyType::Float: return 3.5f;
        case vfx::VFXPropertyType::Vec3:  return glm::vec3(1.0f, 2.0f, 3.0f);
        case vfx::VFXPropertyType::Color: return glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
        case vfx::VFXPropertyType::Int:   return int32_t{2};
        case vfx::VFXPropertyType::Bool:  return true;
        default:                          return vfx::defaultValueFor(type);
        }
    }
}

TEST_SUITE("VFXOverrideApplier")
{
    TEST_CASE("typed overrides set the matching emitter optional")
    {
        services::VFXEmitterOverrides out;
        for (const auto& parameter : vfx::kExposedParameters)
        {
            CHECK(services::applyOverride(out, parameter.name, sampleValue(parameter.type)));
        }

        CHECK(*out.spawnRate == doctest::Approx(3.5f));
        CHECK(*out.lifetime == doctest::Approx(3.5f));
        CHECK(*out.startSize == doctest::Approx(3.5f));
        CHECK(*out.startSpeed == doctest::Approx(3.5f));
        CHECK(*out.stretchMultiplier == doctest::Approx(3.5f));
        CHECK(*out.windStrength == doctest::Approx(3.5f));
        CHECK(*out.gravityStrength == doctest::Approx(3.5f));
        CHECK(*out.softParticleDistance == doctest::Approx(3.5f));
        CHECK(*out.lightingInfluence == doctest::Approx(3.5f));
        CHECK(*out.collisionLifetimeLoss == doctest::Approx(3.5f));
        CHECK(*out.coneSpread == doctest::Approx(3.5f));
        CHECK(out.renderMode == 2);
        CHECK(out.collisionEnabled == true);
        CHECK(out.emitDirection->x == doctest::Approx(1.0f));
        CHECK(out.windDirection->y == doctest::Approx(2.0f));
        CHECK(out.gravityDirection->z == doctest::Approx(3.0f));
        CHECK(out.shapeDimensions->z == doctest::Approx(3.0f));
        CHECK(out.startColor->w == doctest::Approx(0.4f));
        CHECK(out.blendMode == 2);
        CHECK(out.sortOrder == 2);
        CHECK(out.colorTint->x == doctest::Approx(0.1f));
        CHECK(out.colorTint->w == doctest::Approx(0.4f));
    }

    TEST_CASE("VK-1657 typed overrides reject out-of-range render state and tint")
    {
        services::VFXEmitterOverrides out;
        CHECK_FALSE(services::applyOverride(out, "blendMode", int32_t{4}));
        CHECK_FALSE(services::applyOverride(out, "sortOrder", int32_t{300}));
        CHECK_FALSE(services::applyOverride(out, "colorTint", glm::vec4(-1.0f, 1.0f, 1.0f, 1.0f)));
        CHECK_FALSE(services::applyOverride(out, "spawnRate", std::numeric_limits<float>::quiet_NaN()));
        CHECK_FALSE(out.blendMode.has_value());
        CHECK_FALSE(out.sortOrder.has_value());
        CHECK_FALSE(out.colorTint.has_value());
        CHECK_FALSE(out.spawnRate.has_value());

        CHECK(services::applyOverride(out, "blendMode", int32_t{3}));
        CHECK(services::applyOverride(out, "sortOrder", int32_t{-256}));
        CHECK(out.blendMode == 3);
        CHECK(out.sortOrder == -256);
    }

    TEST_CASE("VK-1657 script scalar and vector paths for blend, sort and tint")
    {
        services::VFXEmitterOverrides out;
        CHECK(services::applyScalarOverride(out, "blendMode", 1.0f));
        CHECK(out.blendMode == 1);
        CHECK_FALSE(services::applyScalarOverride(out, "blendMode", 7.0f));
        CHECK_FALSE(services::applyScalarOverride(out, "blendMode", -1.0f));
        CHECK_FALSE(services::applyScalarOverride(out, "blendMode", std::numeric_limits<float>::infinity()));
        CHECK(out.blendMode == 1); // the rejected calls left it alone

        CHECK(services::applyScalarOverride(out, "sortOrder", 999.0f));
        CHECK(out.sortOrder == 256);
        CHECK(services::applyScalarOverride(out, "sortOrder", -999.0f));
        CHECK(out.sortOrder == -256);
        CHECK_FALSE(services::applyScalarOverride(out, "sortOrder", std::numeric_limits<float>::quiet_NaN()));

        CHECK(services::applyVectorOverride(out, "colorTint", glm::vec4(1.0f, 0.5f, 0.1f, 1.0f)));
        CHECK(out.colorTint->y == doctest::Approx(0.5f));
        CHECK_FALSE(services::applyVectorOverride(out, "colorTint", glm::vec4(1.0f, -0.5f, 0.1f, 1.0f)));
        CHECK(out.colorTint->y == doctest::Approx(0.5f));
    }

    TEST_CASE("wrong typed values fail without mutation")
    {
        services::VFXEmitterOverrides out;
        CHECK_FALSE(services::applyOverride(out, "collisionEnabled", 1.0f));
        CHECK_FALSE(out.collisionEnabled.has_value());
        CHECK_FALSE(services::applyOverride(out, "startColor", glm::vec3(1.0f)));
        CHECK_FALSE(out.startColor.has_value());
        CHECK_FALSE(services::applyOverride(out, "notReal", 1.0f));
    }

    TEST_CASE("legacy scalar and vector shims preserve old coercions")
    {
        services::VFXEmitterOverrides out;
        CHECK(services::applyScalarOverride(out, "renderMode", 2.9f));
        CHECK(services::applyScalarOverride(out, "collisionEnabled", 0.5f));
        CHECK(services::applyVectorOverride(out, "emitDirection", glm::vec4(0.0f, 1.0f, 0.0f, 9.0f)));
        CHECK(services::applyVectorOverride(out, "startColor", glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)));

        CHECK(out.renderMode == 2);
        CHECK(out.collisionEnabled == true);
        CHECK(out.emitDirection->y == doctest::Approx(1.0f));
        CHECK(out.startColor->z == doctest::Approx(0.25f));
    }
}
