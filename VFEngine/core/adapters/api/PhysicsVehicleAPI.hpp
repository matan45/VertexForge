#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class PhysicsVehicleAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
    };
}
