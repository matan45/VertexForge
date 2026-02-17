#pragma once

// Internal helper — includers must first include ScriptInterpreter.hpp (mType/Windows macro order),
// then EventDispatcher.hpp and PostProcessEvents.hpp before this header.

namespace core::api::detail
{
    template<typename Mutator>
    value::Value modifySettings(events::EventDispatcher& dispatcher, Mutator&& mutator)
    {
        auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
        mutator(settings);
        events::postprocess::ApplyPostProcessSettingsCommand cmd;
        cmd.settings = settings;
        dispatcher.execute(cmd);
        return value::Value(std::monostate{});
    }
}
