#pragma once

namespace handlers
{
	// VK-1655: serves the plugin post-process effect events (list, enable) over the
	// PluginContextImpl of every loaded plugin. Lives in Editor.exe because the contexts
	// belong to the Plugin StaticLib linked here; the MCP tools reach an effect only by its
	// handle id. Dispatch from the main thread outside the frame task graph (the MCP drain):
	// plugins change their effects from onUpdate, which runs in the graph's Plugins task.
	class PluginPostProcessEffectHandler
	{
	public:
		PluginPostProcessEffectHandler() = default;
		~PluginPostProcessEffectHandler();

		void registerEventHandlers();
		void unregisterEventHandlers();
	};
}
