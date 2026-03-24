// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "SceneAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../../services/events/scene/SceneManagementEvents.hpp"
#include "../scripting/ScriptSceneEventBridge.hpp"
#include <filesystem>

namespace core::api
{
    void SceneAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_scene_load(path) -> void
        // Replaces the current scene entirely
        interpreter->registerNativeFunction("_native_scene_load",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Scene.load: missing path argument");
                    return value::Value(std::monostate{});
                }
                std::string path = extractString(args[0], "Scene.load");

                events::scene::NewSceneCommand newCmd;
                dispatcher.execute(newCmd);

                events::scene::LoadSceneCommand loadCmd;
                loadCmd.filePath = path;
                dispatcher.execute(loadCmd);

                return value::Value(std::monostate{});
            });

        // _native_scene_loadAdditive(path) -> string (scene name)
        // Loads a scene additively without replacing the current one
        interpreter->registerNativeFunction("_native_scene_loadAdditive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Scene.loadAdditive: missing path argument");
                    return value::Value(std::string(""));
                }
                std::string path = extractString(args[0], "Scene.loadAdditive");

                // Auto-generate scene name from filename stem
                std::filesystem::path p(path);
                std::string sceneName = p.stem().string();

                // If a scene with this name already exists, append a counter
                events::scene::IsSceneLoadedQuery checkQuery;
                checkQuery.sceneName = sceneName;
                if (dispatcher.query(checkQuery))
                {
                    int counter = 2;
                    std::string baseName = sceneName;
                    while (dispatcher.query(checkQuery))
                    {
                        sceneName = baseName + "_" + std::to_string(counter++);
                        checkQuery.sceneName = sceneName;
                    }
                }

                events::scene::LoadSceneAdditiveCommand cmd;
                cmd.scenePath = path;
                cmd.sceneName = sceneName;
                dispatcher.execute(cmd);

                return value::Value(sceneName);
            });

        // _native_scene_unload(name) -> void
        interpreter->registerNativeFunction("_native_scene_unload",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Scene.unload: missing name argument");
                    return value::Value(std::monostate{});
                }
                std::string name = extractString(args[0], "Scene.unload");

                events::scene::UnloadAdditiveSceneCommand cmd;
                cmd.sceneName = name;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_scene_loadAsync(path, callback) -> void
        // Queues an async scene load, callback is invoked when done
        interpreter->registerNativeFunction("_native_scene_loadAsync",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    vfLogError("[Script] Scene.loadAsync: missing path or callback argument");
                    return value::Value(std::monostate{});
                }
                std::string path = extractString(args[0], "Scene.loadAsync");
                value::Value callback = args[1];

                // Store callback for later resolution by the event bridge
                ScriptSceneEventBridge::storeAsyncCallback(path, callback);

                events::scene::LoadSceneCommand cmd;
                cmd.filePath = path;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_scene_getActive() -> string
        interpreter->registerNativeFunction("_native_scene_getActive",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::scene::GetActiveSceneQuery query;
                return value::Value(dispatcher.query(query));
            });

        // _native_scene_setActive(name) -> void
        interpreter->registerNativeFunction("_native_scene_setActive",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    vfLogError("[Script] Scene.setActive: missing name argument");
                    return value::Value(std::monostate{});
                }
                std::string name = extractString(args[0], "Scene.setActive");

                events::scene::SetActiveSceneCommand cmd;
                cmd.sceneName = name;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // _native_scene_isLoaded(name) -> bool
        interpreter->registerNativeFunction("_native_scene_isLoaded",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                std::string name = extractString(args[0], "Scene.isLoaded");

                events::scene::IsSceneLoadedQuery query;
                query.sceneName = name;
                return value::Value(dispatcher.query(query));
            });
    }
}
