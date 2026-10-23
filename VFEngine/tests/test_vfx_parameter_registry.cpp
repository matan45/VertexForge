#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <vfx/VFXOverrideNames.hpp>
#include <vfx/VFXParameterRegistry.hpp>

#include <limits>
#include <unordered_set>

TEST_SUITE("VFXParameterRegistry")
{
    TEST_CASE("registry has unique names and targets for every runtime override")
    {
        CHECK(vfx::kExposedParameters.size() == 21);

        std::unordered_set<std::string_view> names;
        std::unordered_set<int> targets;
        for (const auto& parameter : vfx::kExposedParameters)
        {
            CHECK(names.insert(parameter.name).second);
            CHECK(targets.insert(static_cast<int>(parameter.target)).second);
            CHECK(vfx::findExposedParameter(parameter.name) == &parameter);
        }

        CHECK(vfx::overridenames::scalarOverrides.size() == 15);
        CHECK(vfx::overridenames::vectorOverrides.size() == 6);
        CHECK(vfx::overridenames::isScalarOverride("renderMode"));
        CHECK(vfx::overridenames::isScalarOverride("collisionEnabled"));
        CHECK(vfx::overridenames::isScalarOverride("blendMode"));
        CHECK(vfx::overridenames::isScalarOverride("sortOrder"));
        CHECK(vfx::overridenames::isVectorOverride("startColor"));
        CHECK(vfx::overridenames::isVectorOverride("emitDirection"));
        CHECK(vfx::overridenames::isVectorOverride("colorTint"));
    }

    TEST_CASE("VK-1657 render-state and tint parameters are appended with their types")
    {
        // Appended after startColor so the sequence editor's "+ Override" default (front()) stays spawnRate.
        CHECK(vfx::kExposedParameters.front().name == "spawnRate");

        const vfx::VFXExposedParameter* blend = vfx::findExposedParameter(vfx::overridenames::blendMode);
        const vfx::VFXExposedParameter* sort = vfx::findExposedParameter(vfx::overridenames::sortOrder);
        const vfx::VFXExposedParameter* tint = vfx::findExposedParameter(vfx::overridenames::colorTint);
        REQUIRE(blend != nullptr);
        REQUIRE(sort != nullptr);
        REQUIRE(tint != nullptr);
        CHECK(blend->type == vfx::VFXPropertyType::Int);
        CHECK(blend->target == vfx::VFXOverrideTarget::BlendMode);
        CHECK(sort->type == vfx::VFXPropertyType::Int);
        CHECK(sort->target == vfx::VFXOverrideTarget::SortOrder);
        CHECK(tint->type == vfx::VFXPropertyType::Color);
        CHECK(tint->target == vfx::VFXOverrideTarget::ColorTint);
    }

    TEST_CASE("isValidOverrideValue enforces type, finiteness and the VK-1657 ranges")
    {
        const vfx::VFXExposedParameter& spawnRate = *vfx::findExposedParameter("spawnRate");
        const vfx::VFXExposedParameter& blend = *vfx::findExposedParameter("blendMode");
        const vfx::VFXExposedParameter& sort = *vfx::findExposedParameter("sortOrder");
        const vfx::VFXExposedParameter& tint = *vfx::findExposedParameter("colorTint");
        const vfx::VFXExposedParameter& startColor = *vfx::findExposedParameter("startColor");
        const vfx::VFXExposedParameter& emitDirection = *vfx::findExposedParameter("emitDirection");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        CHECK(vfx::isValidOverrideValue(spawnRate, 40.0f));
        CHECK_FALSE(vfx::isValidOverrideValue(spawnRate, int32_t{40}));
        CHECK_FALSE(vfx::isValidOverrideValue(spawnRate, nan));
        CHECK_FALSE(vfx::isValidOverrideValue(spawnRate, inf));
        CHECK_FALSE(vfx::isValidOverrideValue(emitDirection, glm::vec3(0.0f, nan, 0.0f)));

        CHECK(vfx::isValidOverrideValue(blend, int32_t{0}));
        CHECK(vfx::isValidOverrideValue(blend, int32_t{3}));
        CHECK_FALSE(vfx::isValidOverrideValue(blend, int32_t{4}));
        CHECK_FALSE(vfx::isValidOverrideValue(blend, int32_t{-1}));

        CHECK(vfx::isValidOverrideValue(sort, int32_t{vfx::kMinSortOrder}));
        CHECK(vfx::isValidOverrideValue(sort, int32_t{vfx::kMaxSortOrder}));
        CHECK_FALSE(vfx::isValidOverrideValue(sort, int32_t{vfx::kMaxSortOrder + 1}));
        CHECK_FALSE(vfx::isValidOverrideValue(sort, int32_t{vfx::kMinSortOrder - 1}));

        CHECK(vfx::isValidOverrideValue(tint, glm::vec4(1.0f, 0.6f, 0.25f, 1.0f)));
        CHECK(vfx::isValidOverrideValue(tint, glm::vec4(4.0f, 0.0f, 0.0f, 1.0f)));
        CHECK_FALSE(vfx::isValidOverrideValue(tint, glm::vec4(-0.1f, 1.0f, 1.0f, 1.0f)));
        CHECK_FALSE(vfx::isValidOverrideValue(tint, glm::vec4(1.0f, 1.0f, nan, 1.0f)));
        // Only the tint is non-negative: a start colour may hold any finite value.
        CHECK(vfx::isValidOverrideValue(startColor, glm::vec4(-1.0f, 0.0f, 0.0f, 1.0f)));

        CHECK(vfx::isValidOverride({"sortOrder", int32_t{12}}));
        CHECK_FALSE(vfx::isValidOverride({"notAParameter", 1.0f}));
        CHECK_FALSE(vfx::isValidOverride({"sortOrder", 12.0f}));
    }

    TEST_CASE("type matching and defaults follow VFXPropertyValue alternatives")
    {
        CHECK(vfx::valueMatchesType(1.0f, vfx::VFXPropertyType::Float));
        CHECK(vfx::valueMatchesType(glm::vec3(1.0f), vfx::VFXPropertyType::Vec3));
        CHECK(vfx::valueMatchesType(glm::vec4(1.0f), vfx::VFXPropertyType::Color));
        CHECK(vfx::valueMatchesType(int32_t{2}, vfx::VFXPropertyType::Int));
        CHECK(vfx::valueMatchesType(true, vfx::VFXPropertyType::Bool));
        CHECK_FALSE(vfx::valueMatchesType(1.0f, vfx::VFXPropertyType::Bool));
        CHECK_FALSE(vfx::valueMatchesType(glm::vec4(1.0f), vfx::VFXPropertyType::Vec3));

        CHECK(std::holds_alternative<float>(vfx::defaultValueFor(vfx::VFXPropertyType::Float)));
        CHECK(std::holds_alternative<glm::vec3>(vfx::defaultValueFor(vfx::VFXPropertyType::Vec3)));
        CHECK(std::holds_alternative<glm::vec4>(vfx::defaultValueFor(vfx::VFXPropertyType::Color)));
        CHECK(std::holds_alternative<int32_t>(vfx::defaultValueFor(vfx::VFXPropertyType::Int)));
        CHECK(std::holds_alternative<bool>(vfx::defaultValueFor(vfx::VFXPropertyType::Bool)));
    }
}
