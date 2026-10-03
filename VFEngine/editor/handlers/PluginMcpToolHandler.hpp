#pragma once

namespace handlers
{
	// VK-1652: serves the plugin-contributed MCP tool events (list, invoke) over
	// plugin::PluginMcpToolRegistry. Lives in Editor.exe because the registry and the
	// plugin handlers it holds belong to the Plugin StaticLib linked here; the MCP
	// server (PluginToolBridge) reaches them only by qualified tool name.
	class PluginMcpToolHandler
	{
	public:
		PluginMcpToolHandler() = default;
		~PluginMcpToolHandler();

		void registerEventHandlers();
		void unregisterEventHandlers();
	};
}
