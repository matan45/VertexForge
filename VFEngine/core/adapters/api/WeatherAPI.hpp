#pragma once

namespace services { class ScriptInterpreter; }
namespace events { class EventDispatcher; }

namespace core::api
{
    class WeatherAPI
    {
    public:
        static void registerAPI(services::ScriptInterpreter* interpreter);

    private:
        static void registerControlFunctions(services::ScriptInterpreter* interpreter,
                                              events::EventDispatcher& dispatcher);
        static void registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                            events::EventDispatcher& dispatcher);
    };
}
