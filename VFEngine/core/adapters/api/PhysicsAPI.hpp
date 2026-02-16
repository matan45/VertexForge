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
        static constexpr float MAX_RAYCAST_DISTANCE = 10000.0f;
        static constexpr int MAX_RAYCASTS_PER_FRAME = 100;

        inline static int raycastCountThisFrame = 0;

    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void beginFrame();
    };
}
