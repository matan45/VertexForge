#pragma once

namespace services
{
    class ScriptInterpreter;
}

namespace core::api
{
    class NavmeshAPI
    {
    private:
        static constexpr int MAX_PATH_QUERIES_PER_FRAME = 50;

        inline static int pathQueryCountThisFrame = 0;

    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);
        static void beginFrame();
    };
}
