#include "ActionMappingServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include "../../events/input/InputContextEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace services {

    void ActionMappingServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerQueryHandler<events::input::IsActionDownQuery>(
            [this](const events::input::IsActionDownQuery& query) {
                return isActionDown(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::IsActionPressedQuery>(
            [this](const events::input::IsActionPressedQuery& query) {
                return isActionPressed(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::IsActionReleasedQuery>(
            [this](const events::input::IsActionReleasedQuery& query) {
                return isActionReleased(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::GetActionBindingsQuery>(
            [this](const events::input::GetActionBindingsQuery& query) {
                return getActionBindings(query.actionName);
            });

        dispatcher.registerQueryHandler<events::input::GetAllActionNamesQuery>(
            [this](const events::input::GetAllActionNamesQuery&) {
                return getAllActionNames();
            });

        dispatcher.registerCommandHandler<events::input::RegisterActionCommand>(
            [this](const events::input::RegisterActionCommand& cmd) {
                registerAction(cmd.actionName, cmd.defaultBindings, cmd.context);
            });

        dispatcher.registerCommandHandler<events::input::SetActionContextCommand>(
            [this](const events::input::SetActionContextCommand& cmd) {
                setActionContext(cmd.actionName, cmd.context);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterActionCommand>(
            [this](const events::input::UnregisterActionCommand& cmd) {
                unregisterAction(cmd.actionName);
            });

        dispatcher.registerCommandHandler<events::input::AddActionBindingCommand>(
            [this](const events::input::AddActionBindingCommand& cmd) {
                addBinding(cmd.actionName, cmd.binding);
            });

        dispatcher.registerCommandHandler<events::input::RemoveActionBindingCommand>(
            [this](const events::input::RemoveActionBindingCommand& cmd) {
                removeBinding(cmd.actionName, cmd.binding);
            });

        dispatcher.registerCommandHandler<events::input::SetActionBindingsCommand>(
            [this](const events::input::SetActionBindingsCommand& cmd) {
                setBindings(cmd.actionName, cmd.bindings);
            });

        dispatcher.registerCommandHandler<events::input::ResetActionBindingsCommand>(
            [this](const events::input::ResetActionBindingsCommand& cmd) {
                resetBindings(cmd.actionName);
            });

        dispatcher.registerCommandHandler<events::input::ResetAllActionBindingsCommand>(
            [this](const events::input::ResetAllActionBindingsCommand&) {
                resetAllBindings();
            });

        dispatcher.registerCommandHandler<events::input::SaveActionBindingsCommand>(
            [this](const events::input::SaveActionBindingsCommand& cmd) {
                return saveBindings(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::input::LoadActionBindingsCommand>(
            [this](const events::input::LoadActionBindingsCommand& cmd) {
                return loadBindings(cmd.filePath);
            });

        dispatcher.registerQueryHandler<events::input::GetAxis1DValueQuery>(
            [this](const events::input::GetAxis1DValueQuery& query) {
                return getAxis1DValue(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAxis2DValueQuery>(
            [this](const events::input::GetAxis2DValueQuery& query) {
                return getAxis2DValue(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAllAxis1DNamesQuery>(
            [this](const events::input::GetAllAxis1DNamesQuery&) {
                return getAllAxis1DNames();
            });

        dispatcher.registerQueryHandler<events::input::GetAllAxis2DNamesQuery>(
            [this](const events::input::GetAllAxis2DNamesQuery&) {
                return getAllAxis2DNames();
            });

        dispatcher.registerQueryHandler<events::input::GetAxis1DDefinitionQuery>(
            [this](const events::input::GetAxis1DDefinitionQuery& query) {
                return getAxis1DDefinition(query.axisName);
            });

        dispatcher.registerQueryHandler<events::input::GetAxis2DDefinitionQuery>(
            [this](const events::input::GetAxis2DDefinitionQuery& query) {
                return getAxis2DDefinition(query.axisName);
            });

        dispatcher.registerCommandHandler<events::input::RegisterAxis1DCommand>(
            [this](const events::input::RegisterAxis1DCommand& cmd) {
                registerAxis1D(cmd.axisName, cmd.positiveAction, cmd.negativeAction);
            });

        dispatcher.registerCommandHandler<events::input::RegisterAxis2DCommand>(
            [this](const events::input::RegisterAxis2DCommand& cmd) {
                registerAxis2D(cmd.axisName, cmd.upAction, cmd.downAction,
                               cmd.leftAction, cmd.rightAction, cmd.normalize);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterAxis1DCommand>(
            [this](const events::input::UnregisterAxis1DCommand& cmd) {
                unregisterAxis1D(cmd.axisName);
            });

        dispatcher.registerCommandHandler<events::input::UnregisterAxis2DCommand>(
            [this](const events::input::UnregisterAxis2DCommand& cmd) {
                unregisterAxis2D(cmd.axisName);
            });

        dispatcher.registerCommandHandler<events::input::CreateContextCommand>(
            [this](const events::input::CreateContextCommand& cmd) {
                createContext(cmd.contextName, cmd.blocking);
            });

        dispatcher.registerCommandHandler<events::input::RemoveContextCommand>(
            [this](const events::input::RemoveContextCommand& cmd) {
                removeContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::PushContextCommand>(
            [this](const events::input::PushContextCommand& cmd) {
                pushContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::PopContextCommand>(
            [this](const events::input::PopContextCommand& cmd) {
                popContext(cmd.contextName);
            });

        dispatcher.registerCommandHandler<events::input::SetContextBlockingCommand>(
            [this](const events::input::SetContextBlockingCommand& cmd) {
                setContextBlocking(cmd.contextName, cmd.blocking);
            });

        dispatcher.registerQueryHandler<events::input::GetActiveContextsQuery>(
            [this](const events::input::GetActiveContextsQuery&) {
                return getActiveContexts();
            });

        dispatcher.registerQueryHandler<events::input::GetAllContextNamesQuery>(
            [this](const events::input::GetAllContextNamesQuery&) {
                return getAllContextNames();
            });

        dispatcher.registerQueryHandler<events::input::IsContextActiveQuery>(
            [this](const events::input::IsContextActiveQuery& query) {
                return isContextActive(query.contextName);
            });

        dispatcher.registerQueryHandler<events::input::GetContextActionsQuery>(
            [this](const events::input::GetContextActionsQuery& query) {
                return getContextActions(query.contextName);
            });

        dispatcher.registerQueryHandler<events::input::GetActionContextQuery>(
            [this](const events::input::GetActionContextQuery& query) {
                return getActionContext(query.actionName);
            });

        dispatcher.registerCommandHandler<events::input::ConsumeActionCommand>(
            [this](const events::input::ConsumeActionCommand& cmd) {
                consumeAction(cmd.actionName);
            });

        dispatcher.registerQueryHandler<events::input::IsActionConsumedQuery>(
            [this](const events::input::IsActionConsumedQuery& query) {
                return isActionConsumed(query.actionName);
            });

        dispatcher.registerCommandHandler<events::input::ClearConsumedActionsCommand>(
            [this](const events::input::ClearConsumedActionsCommand&) {
                clearConsumedActions();
            });

        subscriptions.push_back(
            dispatcher.subscribe<events::project::ProjectLoadedNotification>(
                [this](const events::project::ProjectLoadedNotification& n) {
                    if (!n.project.inputMapping.has_value() || n.project.inputMapping->empty()) {
                        return;
                    }
                    std::filesystem::path mappingPath = *n.project.inputMapping;
                    if (mappingPath.is_relative() && !n.project.workingDirectory.empty()) {
                        mappingPath = std::filesystem::path(n.project.workingDirectory) / mappingPath;
                    }
                    if (!std::filesystem::exists(mappingPath)) {
                        vfLogWarning("Project input mapping not found: {}", mappingPath.string());
                        return;
                    }
                    if (loadBindings(mappingPath.string())) {
                        vfLogInfo("Project input mapping loaded: {}", mappingPath.string());
                    }
                }));
    }

}
