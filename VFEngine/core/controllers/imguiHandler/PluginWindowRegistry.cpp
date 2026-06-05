#include "PluginWindowRegistry.hpp"
#include <algorithm>

namespace controllers::imguiHandler
{
	void PluginWindowRegistry::add(const std::string& pluginName, const std::string& title,
	                               const std::shared_ptr<ImguiWindow>& window)
	{
		entries.push_back({pluginName, title, window, true});
	}

	void PluginWindowRegistry::remove(const std::shared_ptr<ImguiWindow>& window)
	{
		std::erase_if(entries, [&](const Entry& entry) { return entry.window == window; });
	}

	void PluginWindowRegistry::removeByPlugin(const std::string& pluginName)
	{
		std::erase_if(entries, [&](const Entry& entry) { return entry.pluginName == pluginName; });
	}

	void PluginWindowRegistry::draw()
	{
		// Copy in case a draw() registers new windows
		auto entriesCopy = entries;
		for (const auto& entry : entriesCopy)
		{
			if (entry.visible && entry.window)
			{
				entry.window->draw();
			}
		}
	}

	void PluginWindowRegistry::cleanUp()
	{
		entries.clear();
	}
}
