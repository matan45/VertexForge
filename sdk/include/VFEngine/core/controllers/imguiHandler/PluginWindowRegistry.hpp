#pragma once
#include "ImguiWindow.hpp"
#include <memory>
#include <string>
#include <vector>

namespace controllers::imguiHandler
{
	// Registry for plugin-registered editor windows that carry a title.
	// Unlike ImguiWindowHandler (always-on engine windows), entries here have a
	// visibility flag toggled from the editor's "Plugins" main-menu dropdown.
	class PluginWindowRegistry
	{
	public:
		struct Entry
		{
			std::string pluginName;
			std::string title;
			std::shared_ptr<ImguiWindow> window;
			bool visible = true;
			// VK-1365: per-scene soft-disable gate. AND-ed with the user's visible
			// toggle at draw time so neither flag clobbers the other.
			bool pluginActive = true;
		};

	private:
		inline static std::vector<Entry> entries;

	public:
		explicit PluginWindowRegistry() = default;
		~PluginWindowRegistry() = default;

		static void add(const std::string& pluginName, const std::string& title,
		                const std::shared_ptr<ImguiWindow>& window);
		static void remove(const std::shared_ptr<ImguiWindow>& window);
		static void removeByPlugin(const std::string& pluginName);
		// VK-1365: hide/show all of a plugin's windows when it is soft-disabled.
		static void setPluginActive(const std::string& pluginName, bool active);
		static void draw();
		static void cleanUp();

		// For the editor's Plugins menu — visible flags are mutable through this.
		static std::vector<Entry>& getEntries() { return entries; }
	};
}
