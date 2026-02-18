#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PhysicsAnimationAPI
    {
    public:
        static constexpr float MAX_RAGDOLL_IMPULSE_MAGNITUDE = 10000.0f;

        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
