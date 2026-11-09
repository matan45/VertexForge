#include "PluginMcpToolHandler.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/PluginMcpToolEvents.hpp"
#include "core/PluginMcpToolRegistry.hpp"

namespace handlers
{
	PluginMcpToolHandler::~PluginMcpToolHandler()
	{
		unregisterEventHandlers();
	}

	void PluginMcpToolHandler::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerQueryHandler<events::editor::ListPluginMcpToolsQuery>(
			[](const events::editor::ListPluginMcpToolsQuery&) -> std::vector<events::editor::PluginMcpToolInfo> {
				return plugin::PluginMcpToolRegistry::listActive();
			});

		dispatcher.registerCommandHandler<events::editor::InvokePluginMcpToolCommand>(
			[](const events::editor::InvokePluginMcpToolCommand& cmd) -> events::editor::PluginMcpToolInvokeResult {
				return plugin::PluginMcpToolRegistry::invoke(cmd.qualifiedName, cmd.arguments);
			});
	}

	void PluginMcpToolHandler::unregisterEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unregisterQueryHandler<events::editor::ListPluginMcpToolsQuery>();
		dispatcher.unregisterCommandHandler<events::editor::InvokePluginMcpToolCommand>();
	}
}
