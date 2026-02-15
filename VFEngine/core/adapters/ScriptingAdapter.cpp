// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <project/ProjectBuilder.hpp>
#include <project/ProjectConfigParser.hpp>

#include "ScriptingAdapter.hpp"
#include "NativeAPIRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <regex>

#include "print/EditorLogger.hpp"
#include "events/PhysicsEvents.hpp"
#include "events/UIEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

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
            subscribeToPhysicsEvents();
            subscribeToUIButtonEvents();
            subscribeToUITextInputEvents();
            subscribeToUICheckboxEvents();
            subscribeToUIDropdownEvents();

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

        unsubscribeFromPhysicsEvents();
        unsubscribeFromUIButtonEvents();
        unsubscribeFromUITextInputEvents();
        unsubscribeFromUICheckboxEvents();
        unsubscribeFromUIDropdownEvents();

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

            cleanScripts(manifestPath);

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

            project::ProjectConfigParser parser;
            auto config = parser.parse(manifestPath);

            if (config)
            {
                project::ProjectBuilder builder;
                builder.clean(*config);
            }

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
        std::filesystem::path manifestDir = std::filesystem::path(manifestPath).parent_path();
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

        if (!compiled)
        {
            setError(services::ScriptError::Type::Runtime,
                     "Scripts not compiled. Click 'Build Scripts' first.");
            vfLogError("[Script] Scripts not compiled. Click 'Build Scripts' first.");
            return std::nullopt;
        }

        try
        {
            std::string fullPath = scriptLibraryPath.empty() ? scriptPath : scriptLibraryPath + "/" + scriptPath;

            std::string className;
            auto pathIt = pathToClassName.find(scriptPath);
            if (pathIt != pathToClassName.end())
            {
                className = pathIt->second;
            }
            else
            {
                className = extractClassName(fullPath);
                if (className.empty())
                {
                    setError(services::ScriptError::Type::Compile,
                             "Could not find class definition in script", scriptPath);
                    return std::nullopt;
                }
                pathToClassName[scriptPath] = className;
            }

            auto instance = interpreter->createObject(className);
            uint64_t instanceId = nextInstanceId++;

            instanceToClassName[instanceId] = className;
            instanceToEntity[instanceId] = entity;
            instanceToObject[instanceId] = std::any(instance);

            // Cache implemented interfaces for collision/trigger callbacks
            std::unordered_set<std::string> interfaces;
            if (interpreter->classImplementsInterface(className, "ICollisionListener"))
            {
                interfaces.insert("ICollisionListener");
            }
            if (interpreter->classImplementsInterface(className, "ITriggerListener"))
            {
                interfaces.insert("ITriggerListener");
            }
            if (interpreter->classImplementsInterface(className, "IUIButtonListener"))
            {
                interfaces.insert("IUIButtonListener");
            }
            if (interpreter->classImplementsInterface(className, "IUITextInputListener"))
            {
                interfaces.insert("IUITextInputListener");
            }
            if (interpreter->classImplementsInterface(className, "IUICheckboxListener"))
            {
                interfaces.insert("IUICheckboxListener");
            }
            if (interpreter->classImplementsInterface(className, "IUIDropdownListener"))
            {
                interfaces.insert("IUIDropdownListener");
            }
            instanceToInterfaces[instanceId] = std::move(interfaces);

            instanceToPlaybackState[instanceId] = services::ScriptPlaybackState::Stopped;

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
            instanceToInterfaces.erase(instanceId);
            instanceToPlaybackState.erase(instanceId);
        }
    }

    void ScriptingAdapter::unloadAllScripts()
    {
        instanceToClassName.clear();
        instanceToEntity.clear();
        instanceToObject.clear();
        instanceToInterfaces.clear();
        instanceToPlaybackState.clear();
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
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, "onStart", {});
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

        auto stateIt = instanceToPlaybackState.find(instanceId);
        if (stateIt == instanceToPlaybackState.end() ||
            stateIt->second != services::ScriptPlaybackState::Playing)
        {
            return;
        }

        try
        {
            auto objIt = instanceToObject.find(instanceId);
            if (objIt != instanceToObject.end())
            {
                NativeAPIRegistry::setCurrentEntity(instanceToEntity[instanceId]);
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

    void ScriptingAdapter::playVFX(uint64_t instanceId)
    {
        if (!isScriptLoaded(instanceId))
        {
            vfLogWarning("[ScriptingAdapter] playScript: script {} not loaded", instanceId);
            return;
        }

        auto& state = instanceToPlaybackState[instanceId];
        if (state == services::ScriptPlaybackState::Stopped)
        {
            callOnStart(instanceId);
        }
        state = services::ScriptPlaybackState::Playing;
        vfLogInfo("[ScriptingAdapter] Script {} now playing", instanceId);
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
        std::ifstream file(scriptPath);
        if (!file.is_open())
        {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Pattern: @Script followed by optional whitespace/newlines, then class ClassName
        std::regex scriptAnnotationPattern(R"(@Script\s+(?:public\s+)?class\s+(\w+)\b)");
        std::smatch match;

        if (std::regex_search(content, match, scriptAnnotationPattern))
        {
            return match[1].str();
        }

        // Fallback for backwards compatibility
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

        collisionStartToken = dispatcher.subscribe<::events::physics::CollisionStartNotification>(
            [this](const ::events::physics::CollisionStartNotification& notif)
            {
                dispatchCollisionCallback("onCollisionEnter", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionEnter", notif.entityB, notif.entityA);
            });

        collisionEndToken = dispatcher.subscribe<::events::physics::CollisionEndNotification>(
            [this](const ::events::physics::CollisionEndNotification& notif)
            {
                dispatchCollisionCallback("onCollisionExit", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionExit", notif.entityB, notif.entityA);
            });

        triggerEnterToken = dispatcher.subscribe<::events::physics::TriggerEnterNotification>(
            [this](const ::events::physics::TriggerEnterNotification& notif)
            {
                dispatchCollisionCallback("onTriggerEnter", notif.triggerEntity, notif.otherEntity);
            });

        triggerExitToken = dispatcher.subscribe<::events::physics::TriggerExitNotification>(
            [this](const ::events::physics::TriggerExitNotification& notif)
            {
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
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.valid(static_cast<entt::entity>(self.id)))
        {
            return;
        }

        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(self.id));

        if (!scriptComp)
        {
            return;
        }

        std::string requiredInterface;
        std::string methodStr(methodName);
        if (methodStr == "onCollisionEnter" || methodStr == "onCollisionExit")
        {
            requiredInterface = "ICollisionListener";
        }
        else if (methodStr == "onTriggerEnter" || methodStr == "onTriggerExit")
        {
            requiredInterface = "ITriggerListener";
        }
        else
        {
            return;
        }

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id == self.id)
            {
                auto interfaceIt = instanceToInterfaces.find(instanceId);
                if (interfaceIt == instanceToInterfaces.end() ||
                    interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                {
                    continue;
                }

                auto objIt = instanceToObject.find(instanceId);
                if (objIt != instanceToObject.end())
                {
                    try
                    {
                        NativeAPIRegistry::setCurrentEntity(self);
                        auto& instance = std::any_cast<value::Value&>(objIt->second);
                        interpreter->callMethod(instance, methodName,
                                                {value::Value(static_cast<int>(other.id))});
                    }
                    catch (const std::exception& e)
                    {
                        vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
                    }
                }
            }
        }
    }

    void ScriptingAdapter::subscribeToUIButtonEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        buttonClickedToken = dispatcher.subscribe<::events::ui::UIButtonClickedNotification>(
            [this](const ::events::ui::UIButtonClickedNotification& notif)
            {
                dispatchUIButtonCallback("onButtonClicked", notif.entity, notif.entityName);
            });

        buttonPressedToken = dispatcher.subscribe<::events::ui::UIButtonPressedNotification>(
            [this](const ::events::ui::UIButtonPressedNotification& notif)
            {
                dispatchUIButtonCallback("onButtonPressed", notif.entity, notif.entityName);
            });

        buttonReleasedToken = dispatcher.subscribe<::events::ui::UIButtonReleasedNotification>(
            [this](const ::events::ui::UIButtonReleasedNotification& notif)
            {
                dispatchUIButtonCallback("onButtonReleased", notif.entity, notif.entityName);
            });

        buttonHoverEnterToken = dispatcher.subscribe<::events::ui::UIButtonHoverEnterNotification>(
            [this](const ::events::ui::UIButtonHoverEnterNotification& notif)
            {
                dispatchUIButtonCallback("onButtonHoverEnter", notif.entity, notif.entityName);
            });

        buttonHoverExitToken = dispatcher.subscribe<::events::ui::UIButtonHoverExitNotification>(
            [this](const ::events::ui::UIButtonHoverExitNotification& notif)
            {
                dispatchUIButtonCallback("onButtonHoverExit", notif.entity, notif.entityName);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to UI button events");
    }

    void ScriptingAdapter::unsubscribeFromUIButtonEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (buttonClickedToken.isValid())
        {
            dispatcher.unsubscribe(buttonClickedToken);
        }
        if (buttonPressedToken.isValid())
        {
            dispatcher.unsubscribe(buttonPressedToken);
        }
        if (buttonReleasedToken.isValid())
        {
            dispatcher.unsubscribe(buttonReleasedToken);
        }
        if (buttonHoverEnterToken.isValid())
        {
            dispatcher.unsubscribe(buttonHoverEnterToken);
        }
        if (buttonHoverExitToken.isValid())
        {
            dispatcher.unsubscribe(buttonHoverExitToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from UI button events");
    }

    void ScriptingAdapter::dispatchUIButtonCallback(const char* methodName,
                                                     ::services::EntityHandle buttonEntity,
                                                     const std::string& entityName)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find("IUIButtonListener") == interfaces.end())
            {
                continue;
            }

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end())
            {
                continue;
            }

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt != instanceToEntity.end())
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
            }

            try
            {
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName,
                    {value::Value(static_cast<int>(buttonEntity.id)),
                     value::Value(entityName)});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
            }
        }
    }

    // ============================================
    // UI TextInput event helpers
    // ============================================

    void ScriptingAdapter::subscribeToUITextInputEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        textInputSubmitToken = dispatcher.subscribe<::events::ui::UITextInputSubmitNotification>(
            [this](const ::events::ui::UITextInputSubmitNotification& notif)
            {
                dispatchUITextInputCallback("onTextInputSubmit", notif.entity, notif.entityName, notif.text);
            });

        textInputChangedToken = dispatcher.subscribe<::events::ui::UITextInputChangedNotification>(
            [this](const ::events::ui::UITextInputChangedNotification& notif)
            {
                dispatchUITextInputCallback("onTextInputChanged", notif.entity, notif.entityName, notif.text);
            });

        textInputFocusedToken = dispatcher.subscribe<::events::ui::UITextInputFocusedNotification>(
            [this](const ::events::ui::UITextInputFocusedNotification& notif)
            {
                dispatchUITextInputCallback("onTextInputFocused", notif.entity, notif.entityName);
            });

        textInputUnfocusedToken = dispatcher.subscribe<::events::ui::UITextInputUnfocusedNotification>(
            [this](const ::events::ui::UITextInputUnfocusedNotification& notif)
            {
                dispatchUITextInputCallback("onTextInputUnfocused", notif.entity, notif.entityName);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to UI text input events");
    }

    void ScriptingAdapter::unsubscribeFromUITextInputEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (textInputSubmitToken.isValid())
        {
            dispatcher.unsubscribe(textInputSubmitToken);
        }
        if (textInputChangedToken.isValid())
        {
            dispatcher.unsubscribe(textInputChangedToken);
        }
        if (textInputFocusedToken.isValid())
        {
            dispatcher.unsubscribe(textInputFocusedToken);
        }
        if (textInputUnfocusedToken.isValid())
        {
            dispatcher.unsubscribe(textInputUnfocusedToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from UI text input events");
    }

    void ScriptingAdapter::dispatchUITextInputCallback(const char* methodName,
                                                        ::services::EntityHandle entity,
                                                        const std::string& entityName,
                                                        const std::string& text)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find("IUITextInputListener") == interfaces.end())
            {
                continue;
            }

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end())
            {
                continue;
            }

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt != instanceToEntity.end())
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
            }

            try
            {
                auto& instance = std::any_cast<value::Value&>(objIt->second);

                std::string_view method(methodName);
                if (text.empty() &&
                    (method == "onTextInputFocused" ||
                     method == "onTextInputUnfocused"))
                {
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(entity.id)),
                         value::Value(entityName)});
                }
                else
                {
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(entity.id)),
                         value::Value(entityName),
                         value::Value(text)});
                }
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
            }
        }
    }

    // ============================================
    // UI Checkbox event helpers
    // ============================================

    void ScriptingAdapter::subscribeToUICheckboxEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        checkboxToggledToken = dispatcher.subscribe<::events::ui::UICheckboxToggledNotification>(
            [this](const ::events::ui::UICheckboxToggledNotification& notif)
            {
                dispatchUICheckboxCallback("onCheckboxToggled", notif.entity, notif.entityName,
                                           notif.newCheckedState, notif.previousCheckedState);
            });

        checkboxHoverEnterToken = dispatcher.subscribe<::events::ui::UICheckboxHoverEnterNotification>(
            [this](const ::events::ui::UICheckboxHoverEnterNotification& notif)
            {
                dispatchUICheckboxCallback("onCheckboxHoverEnter", notif.entity, notif.entityName);
            });

        checkboxHoverExitToken = dispatcher.subscribe<::events::ui::UICheckboxHoverExitNotification>(
            [this](const ::events::ui::UICheckboxHoverExitNotification& notif)
            {
                dispatchUICheckboxCallback("onCheckboxHoverExit", notif.entity, notif.entityName);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to UI checkbox events");
    }

    void ScriptingAdapter::unsubscribeFromUICheckboxEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (checkboxToggledToken.isValid())
        {
            dispatcher.unsubscribe(checkboxToggledToken);
        }
        if (checkboxHoverEnterToken.isValid())
        {
            dispatcher.unsubscribe(checkboxHoverEnterToken);
        }
        if (checkboxHoverExitToken.isValid())
        {
            dispatcher.unsubscribe(checkboxHoverExitToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from UI checkbox events");
    }

    void ScriptingAdapter::dispatchUICheckboxCallback(const char* methodName,
                                                       ::services::EntityHandle checkboxEntity,
                                                       const std::string& entityName,
                                                       bool newState, bool previousState)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find("IUICheckboxListener") == interfaces.end())
            {
                continue;
            }

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end())
            {
                continue;
            }

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt != instanceToEntity.end())
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
            }

            try
            {
                auto& instance = std::any_cast<value::Value&>(objIt->second);

                std::string_view method(methodName);
                if (method == "onCheckboxToggled")
                {
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(checkboxEntity.id)),
                         value::Value(entityName),
                         value::Value(newState),
                         value::Value(previousState)});
                }
                else
                {
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(checkboxEntity.id)),
                         value::Value(entityName)});
                }
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
            }
        }
    }

    void ScriptingAdapter::subscribeToUIDropdownEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dropdownOpenedToken = dispatcher.subscribe<::events::ui::UIDropdownOpenedNotification>(
            [this](const ::events::ui::UIDropdownOpenedNotification& notif)
            {
                dispatchUIDropdownCallback("onDropdownOpened", notif.entity, notif.entityName);
            });

        dropdownClosedToken = dispatcher.subscribe<::events::ui::UIDropdownClosedNotification>(
            [this](const ::events::ui::UIDropdownClosedNotification& notif)
            {
                dispatchUIDropdownCallback("onDropdownClosed", notif.entity, notif.entityName);
            });

        dropdownSelectionChangedToken = dispatcher.subscribe<::events::ui::UIDropdownSelectionChangedNotification>(
            [this](const ::events::ui::UIDropdownSelectionChangedNotification& notif)
            {
                dispatchUIDropdownCallback("onDropdownSelectionChanged", notif.entity, notif.entityName,
                                           notif.previousIndex, notif.newIndex);
            });

        vfLogInfo("[ScriptingAdapter] Subscribed to UI dropdown events");
    }

    void ScriptingAdapter::unsubscribeFromUIDropdownEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (dropdownOpenedToken.isValid())
        {
            dispatcher.unsubscribe(dropdownOpenedToken);
        }
        if (dropdownClosedToken.isValid())
        {
            dispatcher.unsubscribe(dropdownClosedToken);
        }
        if (dropdownSelectionChangedToken.isValid())
        {
            dispatcher.unsubscribe(dropdownSelectionChangedToken);
        }

        vfLogInfo("[ScriptingAdapter] Unsubscribed from UI dropdown events");
    }

    void ScriptingAdapter::dispatchUIDropdownCallback(const char* methodName,
                                                       ::services::EntityHandle dropdownEntity,
                                                       const std::string& entityName,
                                                       int previousIndex, int newIndex)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find("IUIDropdownListener") == interfaces.end())
            {
                continue;
            }

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end())
            {
                continue;
            }

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt != instanceToEntity.end())
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
            }

            try
            {
                auto& instance = std::any_cast<value::Value&>(objIt->second);

                std::string_view method(methodName);
                if (method == "onDropdownSelectionChanged")
                {
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(dropdownEntity.id)),
                         value::Value(entityName),
                         value::Value(static_cast<int64_t>(previousIndex)),
                         value::Value(static_cast<int64_t>(newIndex))});
                }
                else
                {
                    // onDropdownOpened, onDropdownClosed
                    interpreter->callMethod(instance, methodName,
                        {value::Value(static_cast<int>(dropdownEntity.id)),
                         value::Value(entityName)});
                }
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
