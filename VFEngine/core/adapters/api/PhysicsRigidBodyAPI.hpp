#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PhysicsRigidBodyAPI
    {
    public:
        static constexpr float MAX_FORCE_MAGNITUDE = 100000.0f;
        static constexpr float MAX_IMPULSE_MAGNITUDE = 10000.0f;
        static constexpr float MAX_TORQUE_MAGNITUDE = 10000.0f;
        static constexpr int MAX_FORCE_APPLICATIONS_PER_FRAME = 500;

        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void beginFrame();
    };
}
