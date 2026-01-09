// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <project/ProjectBuilder.hpp>
#include <project/ProjectConfigParser.hpp>

#include "ScriptingAdapter.hpp"
#include "NativeAPIRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <regex>

// Include editor logger for console output
#include "print/EditorLogger.hpp"

// Include physics events for collision callbacks
#include "../../services/events/PhysicsEvents.hpp"

// Include ECS for finding scripts on entities
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"
#include "../../services/data/EntityConversion.hpp"

namespace core
{
    ScriptingAdapter::ScriptingAdapter() = default;

    ScriptingAdapter::~ScriptingAdapter()
    {
        cleanUp();
    }

    bool ScriptingAdapter::init()
    {
        if (initialized)
        {
            return true;
        }

        try
        {
            interpreter = std::make_unique<::services::ScriptInterpreter>();
            
            apiRegistry = std::make_unique<NativeAPIRegistry>(interpreter.get());
            apiRegistry->registerEngineAPIs();

            // Subscribe to physics collision events for script callbacks
            subscribeToPhysicsEvents();

            initialized = true;
            vfLogInfo("[ScriptingAdapter] Initialized mType scripting system");
            return true;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to initialize scripting system: ") + e.what());
            vfLogError("[Script] Init failed: {}", e.what());
            return false;
        }
    }

    void ScriptingAdapter::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        // Unsubscribe from physics events before cleanup
        unsubscribeFromPhysicsEvents();

        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        pathToClassName.clear();

        apiRegistry.reset();
        interpreter.reset();
        initialized = false;

        vfLogInfo("[ScriptingAdapter] Cleaned up scripting system");
    }

    bool ScriptingAdapter::isInitialized() const
    {
        return initialized;
    }

    services::ScriptBuildResult ScriptingAdapter::buildScripts(const std::string& manifestPath)
    {
        services::ScriptBuildResult result;

        if (!initialized)
        {
            result.success = false;
            result.errors.push_back("Scripting system not initialized");
            return result;
        }

        try
        {
            vfLogInfo("[ScriptingAdapter] Building scripts from manifest: {}", manifestPath);

            // Clean first
            cleanScripts(manifestPath);

            // Parse manifest
            project::ProjectConfigParser parser;
            auto config = parser.parse(manifestPath);

            if (!config)
            {
                result.success = false;
                result.errors.push_back("Failed to parse manifest: " + manifestPath);
                return result;
            }

            project::ProjectBuilder builder;
            std::string libraryPath = getLibraryPath(manifestPath);
            auto buildResult = builder.buildLibrary(*config, libraryPath, interpreter->getEnvironment());

            result.success = buildResult.success;
            result.filesCompiled = buildResult.filesCompiled;
            result.filesFailed = buildResult.filesFailed;
            result.errors = buildResult.errors;

            if (result.success)
            {
                compiled = true;
                vfLogInfo("[ScriptingAdapter] Build successful: {} files compiled", result.filesCompiled);
                vfLogInfo("[Script] Build successful: {} files compiled", result.filesCompiled);
            }
            else
            {
                compiled = false;
                vfLogError("[ScriptingAdapter] Build failed with {} errors", result.errors.size());
                for (const auto& error : result.errors)
                {
                    vfLogError("[Script] {}", error);
                }
            }

            return result;
        }
        catch (const std::exception& e)
        {
            result.success = false;
            result.errors.push_back(e.what());
            setError(services::ScriptError::Type::Compile, e.what());
            vfLogError("[Script] Build failed: {}", e.what());
            return result;
        }
    }

    void ScriptingAdapter::cleanScripts(const std::string& manifestPath)
    {
        try
        {
            vfLogInfo("[Script] Cleaning scripts...");

            // Parse manifest to get output directory and clean compiled files
            project::ProjectConfigParser parser;
            auto config = parser.parse(manifestPath);

            if (config)
            {
                project::ProjectBuilder builder;
                builder.clean(*config);
            }

            // Clear cached state
            pathToClassName.clear();
            compiled = false;

            vfLogInfo("[ScriptingAdapter] Clean completed");
            vfLogInfo("[Script] Clean completed");
        }
        catch (const std::exception& e)
        {
            vfLogError("[ScriptingAdapter] Clean failed: {}", e.what());
            vfLogError("[Script] Clean failed: {}", e.what());
        }
    }

    bool ScriptingAdapter::isCompiled() const
    {
        return compiled;
    }

    bool ScriptingAdapter::loadCompiledScripts(const std::string& manifestPath)
    {
        if (!initialized)
        {
            vfLogError("[ScriptingAdapter] Cannot load scripts: not initialized");
            return false;
        }

        if (!compiled)
        {
            vfLogWarning("[ScriptingAdapter] Scripts not compiled. Call buildScripts first.");
            return false;
        }

        try
        {
            std::string libraryPath = getLibraryPath(manifestPath);

            if (!std::filesystem::exists(libraryPath))
            {
                vfLogError("[ScriptingAdapter] Compiled library not found: {}", libraryPath);
                return false;
            }

            vfLogInfo("[ScriptingAdapter] Loading compiled scripts from: {}", libraryPath);
            interpreter->loadCompiledBytecode(libraryPath);

            vfLogInfo("[ScriptingAdapter] Compiled scripts loaded successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("Failed to load compiled scripts: ") + e.what());
            vfLogError("[Script] Failed to load compiled scripts: {}", e.what());
            return false;
        }
    }

    std::string ScriptingAdapter::getLibraryPath(const std::string& manifestPath) const
    {
        // Get the directory containing the manifest
        std::filesystem::path manifestDir = std::filesystem::path(manifestPath).parent_path();

        // The library is output to the 'compiled' subdirectory as 'scripts.mtcLib'
        return (manifestDir / "compiled" / "scripts.mtcLib").string();
    }

    std::optional<services::ScriptInstanceInfo> ScriptingAdapter::loadScript(
        const std::string& scriptPath,
        services::EntityHandle entity)
    {
        if (!initialized)
        {
            setError(services::ScriptError::Type::Runtime, "Scripting system not initialized");
            return std::nullopt;
        }

        // Check if scripts are compiled
        if (!compiled)
        {
            setError(services::ScriptError::Type::Runtime,
                     "Scripts not compiled. Click 'Build Scripts' first.");
            vfLogError("[Script] Scripts not compiled. Click 'Build Scripts' first.");
            return std::nullopt;
        }

        try
        {
            // Get full path for extracting class name
            std::string fullPath = scriptLibraryPath.empty() ? scriptPath : scriptLibraryPath + "/" + scriptPath;

            // Get the class name from cache or extract from file
            std::string className;
            auto pathIt = pathToClassName.find(scriptPath);
            if (pathIt != pathToClassName.end())
            {
                className = pathIt->second;
            }
            else
            {
                // Extract class name from script file
                className = extractClassName(fullPath);
                if (className.empty())
                {
                    setError(services::ScriptError::Type::Compile,
                             "Could not find class definition in script", scriptPath);
                    return std::nullopt;
                }
                pathToClassName[scriptPath] = className;
            }

            // Create script instance from pre-compiled bytecode
            auto instance = interpreter->createObject(className);

            // Assign instance ID
            uint64_t instanceId = nextInstanceId++;

            // Store mappings
            instanceToClassName[instanceId] = className;
            instanceToEntity[instanceId] = entity;
            instanceToObject[instanceId] = std::any(instance);

            // Build result
            services::ScriptInstanceInfo info;
            info.instanceId = instanceId;
            info.className = className;
            info.scriptPath = scriptPath;

            vfLogInfo("[ScriptingAdapter] Loaded script '{}' as class '{}' (instanceId={})",
                      scriptPath, className, instanceId);

            return info;
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime, e.what(), scriptPath);
            vfLogError("[Script] Failed to create script instance '{}': {}", scriptPath, e.what());
            return std::nullopt;
        }
    }

    void ScriptingAdapter::unloadScript(uint64_t instanceId)
    {
        auto it = instanceToClassName.find(instanceId);
        if (it != instanceToClassName.end())
        {
            instanceToClassName.erase(it);
            instanceToEntity.erase(instanceId);
            instanceToObject.erase(instanceId);
        }
    }

    void ScriptingAdapter::unloadAllScripts()
    {
        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        nextInstanceId = 1;
        vfLogInfo("[ScriptingAdapter] All scripts unloaded, instance counter reset");
    }

    bool ScriptingAdapter::isScriptLoaded(uint64_t instanceId) const
    {
        return instanceToClassName.find(instanceId) != instanceToClassName.end();
    }

    void ScriptingAdapter::callOnStart(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] callOnStart: script {} not loaded", instanceId);
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                // Set current entity for callbacks
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);

                // Get the script instance and call onStart
                vfLogInfo("[ScriptingAdapter] Calling interpreter->callMethod for onStart (instance {})",
                          instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onStart", {});

                vfLogInfo("[ScriptingAdapter] onStart completed successfully for instance {}", instanceId);
            }
            else
            {
                vfLogWarning("[ScriptingAdapter] callOnStart: instance {} not found in instanceToObject", instanceId);
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onStart failed: ") + e.what());
            vfLogError("[Script] onStart failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnUpdate(uint64_t instanceId, float deltaTime)
    {
        if (!isScriptLoaded(instanceId))
        {
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                // Set current context for callbacks
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);

                // Get the script instance and call onUpdate with deltaTime argument
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onUpdate", {value::Value(deltaTime)});
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onUpdate failed: ") + e.what());
            vfLogError("[Script] onUpdate failed: {}", e.what());
        }
    }

    void ScriptingAdapter::callOnDestroy(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);

                // Get the script instance and call onDestroy
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onDestroy", {});
            }
        }
        catch (const std::exception& e)
        {
            setError(services::ScriptError::Type::Runtime,
                     std::string("onDestroy failed: ") + e.what());
            vfLogError("[Script] onDestroy failed: {}", e.what());
        }
    }

    std::optional<services::ScriptError> ScriptingAdapter::getLastError() const
    {
        return lastError;
    }

    void ScriptingAdapter::clearError()
    {
        lastError = std::nullopt;
    }

    void ScriptingAdapter::setScriptLibraryPath(const std::string& path)
    {
        scriptLibraryPath = path;
        vfLogInfo("[ScriptingAdapter] Script library path set to: {}", path);
    }

    void ScriptingAdapter::setError(services::ScriptError::Type type, const std::string& message,
                                    const std::string& file, int line)
    {
        lastError = services::ScriptError{type, message, file, line, 0};
    }

    std::string ScriptingAdapter::extractClassName(const std::string& scriptPath)
    {
        // Read the script file and extract the class name
        std::ifstream file(scriptPath);
        if (!file.is_open())
        {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Look for "@Script" annotation followed by a class definition
        // Pattern matches: @Script followed by optional whitespace/newlines, then class ClassName
        std::regex scriptAnnotationPattern(R"(@Script\s+(?:public\s+)?class\s+(\w+)\b)");
        std::smatch match;

        if (std::regex_search(content, match, scriptAnnotationPattern))
        {
            return match[1].str();
        }

        // Fallback: look for any class definition (for backwards compatibility)
        std::regex anyClassPattern(R"(\bclass\s+(\w+)\b)");
        if (std::regex_search(content, match, anyClassPattern))
        {
            return match[1].str();
        }

        return "";
    }

    void ScriptingAdapter::subscribeToPhysicsEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Subscribe to collision start events
        collisionStartToken = dispatcher.subscribe<::events::physics::CollisionStartNotification>(
            [this](const ::events::physics::CollisionStartNotification& notif) {
                // Dispatch to both entities involved in the collision
                dispatchCollisionCallback("onCollisionEnter", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionEnter", notif.entityB, notif.entityA);
            });

        // Subscribe to collision end events
        collisionEndToken = dispatcher.subscribe<::events::physics::CollisionEndNotification>(
            [this](const ::events::physics::CollisionEndNotification& notif) {
                dispatchCollisionCallback("onCollisionExit", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionExit", notif.entityB, notif.entityA);
            });

        // Subscribe to trigger enter events
        triggerEnterToken = dispatcher.subscribe<::events::physics::TriggerEnterNotification>(
            [this](const ::events::physics::TriggerEnterNotification& notif) {
                // Trigger entity receives notification about the other entity
                dispatchCollisionCallback("onTriggerEnter", notif.triggerEntity, notif.otherEntity);
            });

        // Subscribe to trigger exit events
        triggerExitToken = dispatcher.subscribe<::events::physics::TriggerExitNotification>(
            [this](const ::events::physics::TriggerExitNotification& notif) {
                dispatchCollisionCallback("onTriggerExit", notif.triggerEntity, notif.otherEntity);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to physics collision events");
    }

    void ScriptingAdapter::unsubscribeFromPhysicsEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (collisionStartToken.isValid())
        {
            dispatcher.unsubscribe(collisionStartToken);
        }
        if (collisionEndToken.isValid())
        {
            dispatcher.unsubscribe(collisionEndToken);
        }
        if (triggerEnterToken.isValid())
        {
            dispatcher.unsubscribe(triggerEnterToken);
        }
        if (triggerExitToken.isValid())
        {
            dispatcher.unsubscribe(triggerExitToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from physics collision events");
    }

    void ScriptingAdapter::dispatchCollisionCallback(const char* methodName,
        ::services::EntityHandle self, ::services::EntityHandle other)
    {
        // Find all script instances attached to the 'self' entity
        auto& registry = scene::EntityRegistry::getRegistry();

        // Check if entity is valid
        if (!registry.valid(static_cast<entt::entity>(self.id)))
        {
            return;
        }

        // Check if entity has a ScriptComponent
        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(self.id));

        if (!scriptComp)
        {
            return;
        }

        // Find script instances for this entity and call the method
        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id == self.id)
            {
                auto objIt = instanceToObject.find(instanceId);
                if (objIt != instanceToObject.end())
                {
                    try
                    {
                        // Set current entity context
                        NativeAPIRegistry::setCurrentEntity(self);

                        // Get the script instance and call the collision method
                        auto& instance = std::any_cast<value::Value&>(objIt->second);

                        // Pass the other entity's ID as an int argument
                        interpreter->callMethod(instance, methodName,
                            {value::Value(static_cast<int>(other.id))});
                    }
                    catch (const std::exception& e)
                    {
                        // Method might not exist on the script - that's OK, just skip
                        // Only log actual runtime errors, not missing method errors
                        std::string errorMsg = e.what();
                        if (errorMsg.find("Method not found") == std::string::npos &&
                            errorMsg.find("does not exist") == std::string::npos)
                        {
                            vfLogWarning("[ScriptingAdapter] {} callback error: {}",
                                methodName, e.what());
                        }
                    }
                }
            }
        }
    }
}
