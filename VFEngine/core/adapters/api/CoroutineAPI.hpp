#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class CoroutineManager;
}

namespace core::api
{
    class CoroutineAPI
    {
    private:
        inline static CoroutineManager* coroutineManager = nullptr;

    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void setCoroutineManager(CoroutineManager* manager);
    };
}
