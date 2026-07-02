#include <doctest.h>

#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <serialization/SceneSerialization.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <asset/AssetDatabase.hpp>
#include <types/VehicleTypes.hpp>

#include <filesystem>

namespace
{
    namespace fs = std::filesystem;

    fs::path vehicleTestRoot()
    {
        return fs::temp_directory_path() / "vf_vehicle_config_tests";
    }

    void resetVehicleTestRoot()
    {
        std::error_code ec;
        fs::remove_all(vehicleTestRoot(), ec);
        fs::create_directories(vehicleTestRoot(), ec);
        asset::AssetDatabase::instance().clear();
    }

    void checkVec3(const glm::vec3& got, const glm::vec3& want)
    {
        CHECK(got.x == doctest::Approx(want.x));
        CHECK(got.y == doctest::Approx(want.y));
        CHECK(got.z == doctest::Approx(want.z));
    }

    types::VehicleConfig makeCustomVehicleConfig()
    {
        types::VehicleConfig cfg = types::VehicleConfig::createFourWheelCar();
        cfg.controllerType = types::VehicleControllerType::Motorcycle;
        cfg.up = {0.0f, 0.9f, 0.1f};
        cfg.forward = {0.0f, 0.1f, 0.9f};
        cfg.maxPitchRollAngle = 0.75f;
        cfg.engineMaxTorque = 175.0f;
        cfg.engineMinRPM = 900.0f;
        cfg.engineMaxRPM = 9500.0f;
        cfg.maxLeanAngle = 0.6f;
        cfg.leanSpringConstant = 7.5f;
        cfg.leanSpringDamping = 1.25f;
        cfg.collisionTester = types::VehicleCollisionTesterType::CastCylinder;
        cfg.wheelCollisionLayer = 2;
        cfg.maxSlopeAngle = 1.1f;

        cfg.wheels = {
            types::WheelConfig{},
            types::WheelConfig{}
        };
        cfg.wheels[0].position = {0.0f, -0.2f, 0.8f};
        cfg.wheels[0].radius = 0.32f;
        cfg.wheels[0].width = 0.06f;
        cfg.wheels[0].maxSteerAngle = 0.5f;
        cfg.wheels[0].maxBrakeTorque = 600.0f;

        cfg.wheels[1].position = {0.0f, -0.2f, -0.8f};
        cfg.wheels[1].radius = 0.33f;
        cfg.wheels[1].width = 0.07f;
        cfg.wheels[1].driven = true;
        cfg.wheels[1].maxBrakeTorque = 300.0f;

        cfg.differentials = {{-1, 1, 4.5f, 0.5f, 1.0f}};
        return cfg;
    }

    void checkConfigMatches(const types::VehicleConfig& got, const types::VehicleConfig& want)
    {
        CHECK(got.controllerType == want.controllerType);
        checkVec3(got.up, want.up);
        checkVec3(got.forward, want.forward);
        CHECK(got.maxPitchRollAngle == doctest::Approx(want.maxPitchRollAngle));
        CHECK(got.engineMaxTorque == doctest::Approx(want.engineMaxTorque));
        CHECK(got.engineMinRPM == doctest::Approx(want.engineMinRPM));
        CHECK(got.engineMaxRPM == doctest::Approx(want.engineMaxRPM));
        CHECK(got.maxLeanAngle == doctest::Approx(want.maxLeanAngle));
        CHECK(got.leanSpringConstant == doctest::Approx(want.leanSpringConstant));
        CHECK(got.leanSpringDamping == doctest::Approx(want.leanSpringDamping));
        CHECK(got.collisionTester == want.collisionTester);
        CHECK(got.wheelCollisionLayer == want.wheelCollisionLayer);
        CHECK(got.maxSlopeAngle == doctest::Approx(want.maxSlopeAngle));

        REQUIRE(got.wheels.size() == want.wheels.size());
        for (size_t i = 0; i < want.wheels.size(); ++i)
        {
            INFO("wheel " << i);
            checkVec3(got.wheels[i].position, want.wheels[i].position);
            CHECK(got.wheels[i].radius == doctest::Approx(want.wheels[i].radius));
            CHECK(got.wheels[i].width == doctest::Approx(want.wheels[i].width));
            CHECK(got.wheels[i].maxSteerAngle == doctest::Approx(want.wheels[i].maxSteerAngle));
            CHECK(got.wheels[i].maxBrakeTorque == doctest::Approx(want.wheels[i].maxBrakeTorque));
            CHECK(got.wheels[i].driven == want.wheels[i].driven);
        }

        REQUIRE(got.differentials.size() == want.differentials.size());
        for (size_t i = 0; i < want.differentials.size(); ++i)
        {
            INFO("differential " << i);
            CHECK(got.differentials[i].leftWheel == want.differentials[i].leftWheel);
            CHECK(got.differentials[i].rightWheel == want.differentials[i].rightWheel);
            CHECK(got.differentials[i].differentialRatio == doctest::Approx(want.differentials[i].differentialRatio));
            CHECK(got.differentials[i].leftRightSplit == doctest::Approx(want.differentials[i].leftRightSplit));
            CHECK(got.differentials[i].engineTorqueRatio == doctest::Approx(want.differentials[i].engineTorqueRatio));
        }
    }
}

TEST_SUITE("VehicleConfig")
{
    TEST_CASE("vehicle presets produce valid layouts")
    {
        const auto car = types::VehicleConfig::createFourWheelCar();
        CHECK(car.controllerType == types::VehicleControllerType::Wheeled);
        REQUIRE(car.wheels.size() == 4);
        CHECK(car.wheels[0].maxSteerAngle > 0.0f);
        CHECK(car.wheels[2].driven);
        CHECK(types::validateVehicleConfig(car));

        const auto tank = types::VehicleConfig::createTank();
        CHECK(tank.controllerType == types::VehicleControllerType::Tracked);
        REQUIRE(tank.wheels.size() == 18);
        CHECK(tank.wheels.front().trackIndex == 0);
        CHECK(tank.wheels.back().trackIndex == 1);
        CHECK(types::validateVehicleConfig(tank));

        const auto motorcycle = types::VehicleConfig::createMotorcycle();
        CHECK(motorcycle.controllerType == types::VehicleControllerType::Motorcycle);
        REQUIRE(motorcycle.wheels.size() == 2);
        CHECK(motorcycle.wheels[0].maxSteerAngle > 0.0f);
        CHECK(motorcycle.wheels[1].driven);
        CHECK(types::validateVehicleConfig(motorcycle));
    }

    TEST_CASE("vehicle config validation rejects invalid layouts")
    {
        types::VehicleConfig cfg = types::VehicleConfig::createFourWheelCar();
        cfg.wheels.clear();
        CHECK_FALSE(types::validateVehicleConfig(cfg));

        cfg = types::VehicleConfig::createFourWheelCar();
        cfg.wheels[0].radius = 0.0f;
        CHECK_FALSE(types::validateVehicleConfig(cfg));

        cfg = types::VehicleConfig::createFourWheelCar();
        cfg.differentials[0].leftWheel = 99;
        CHECK_FALSE(types::validateVehicleConfig(cfg));

        cfg = types::VehicleConfig::createFourWheelCar();
        cfg.wheelCollisionLayer = 16;
        CHECK_FALSE(types::validateVehicleConfig(cfg));
    }

    TEST_CASE("scene round-trip preserves VehicleComponent config")
    {
        resetVehicleTestRoot();
        const auto want = makeCustomVehicleConfig();

        scene::SceneGraphSystem source;
        source.GetRoot().addOrReplaceComponent<components::VehicleComponent>().config = want;

        const fs::path scenePath = vehicleTestRoot() / "Vehicle.vfScene";
        REQUIRE(serialization::SceneSerialization::saveScene(source, scenePath.string()));

        scene::SceneGraphSystem loaded;
        REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), loaded));
        REQUIRE(loaded.GetRoot().hasComponent<components::VehicleComponent>());

        checkConfigMatches(loaded.GetRoot().getComponent<components::VehicleComponent>().config, want);
    }

    TEST_CASE("prefab round-trip preserves VehicleComponent config")
    {
        resetVehicleTestRoot();
        const auto want = makeCustomVehicleConfig();

        scene::SceneGraphSystem source;
        scene::Entity entity("VehiclePrefabEntity");
        source.addChild(source.GetRoot(), entity);
        entity.addOrReplaceComponent<components::VehicleComponent>().config = want;

        const fs::path prefabPath = vehicleTestRoot() / "Vehicle.vfPrefab";
        REQUIRE(serialization::PrefabSerialization::savePrefab(entity, prefabPath.string()));

        scene::SceneGraphSystem dest;
        auto root = dest.GetRoot();
        auto loadedOpt = serialization::PrefabSerialization::loadPrefab(prefabPath.string(), root, dest);
        REQUIRE(loadedOpt.has_value());
        REQUIRE(loadedOpt->hasComponent<components::VehicleComponent>());

        checkConfigMatches(loadedOpt->getComponent<components::VehicleComponent>().config, want);
    }

    TEST_CASE("cloneOptionalComponents copies VehicleComponent config")
    {
        const auto want = makeCustomVehicleConfig();

        scene::Entity src("VehicleCloneSource");
        scene::Entity dst("VehicleCloneDest");
        src.addOrReplaceComponent<components::VehicleComponent>().config = want;

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::VehicleComponent>());
        checkConfigMatches(dst.getComponent<components::VehicleComponent>().config, want);

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }
}
