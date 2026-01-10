#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PhysicsAPI
    {
    private:
        // Physics API safety limits
        static constexpr float MAX_FORCE_MAGNITUDE = 100000.0f;
        static constexpr float MAX_IMPULSE_MAGNITUDE = 10000.0f;
        static constexpr float MAX_TORQUE_MAGNITUDE = 10000.0f;
        static constexpr float MAX_RAYCAST_DISTANCE = 10000.0f;
        static constexpr int MAX_RAYCASTS_PER_FRAME = 100;
        static constexpr int MAX_FORCE_APPLICATIONS_PER_FRAME = 500;

        // Per-frame rate limiting state
        inline static int raycastCountThisFrame = 0;
        inline static int forceApplicationCountThisFrame = 0;

    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void beginFrame();
    };
}
