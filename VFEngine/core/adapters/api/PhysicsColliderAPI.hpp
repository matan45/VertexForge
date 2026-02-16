#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PhysicsColliderAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
