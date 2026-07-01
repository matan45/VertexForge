#include <doctest.h>

#include <vfx/VFXOverrideNames.hpp>
#include <vfx/VFXParameterRegistry.hpp>

#include <unordered_set>

TEST_SUITE("VFXParameterRegistry")
{
    TEST_CASE("registry has unique names and targets for every runtime override")
    {
        CHECK(vfx::kExposedParameters.size() == 18);

        std::unordered_set<std::string_view> names;
        std::unordered_set<int> targets;
        for (const auto& parameter : vfx::kExposedParameters)
        {
            CHECK(names.insert(parameter.name).second);
            CHECK(targets.insert(static_cast<int>(parameter.target)).second);
            CHECK(vfx::findExposedParameter(parameter.name) == &parameter);
        }

        CHECK(vfx::overridenames::scalarOverrides.size() == 13);
        CHECK(vfx::overridenames::vectorOverrides.size() == 5);
        CHECK(vfx::overridenames::isScalarOverride("renderMode"));
        CHECK(vfx::overridenames::isScalarOverride("collisionEnabled"));
        CHECK(vfx::overridenames::isVectorOverride("startColor"));
        CHECK(vfx::overridenames::isVectorOverride("emitDirection"));
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
