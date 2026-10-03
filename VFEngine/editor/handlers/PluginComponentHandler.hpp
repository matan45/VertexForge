#pragma once
#include "events/scene/PluginComponentEvents.hpp"
#include <nlohmann/json.hpp>
#include <optional>

namespace handlers
{
	// VK-1651: serves the generic plugin-component events (list types, get, set fields,
	// add, remove) over EnTT meta. Lives in Editor.exe because the component bridges are
	// owned by the Plugin StaticLib linked here. Bridges are looked up by name on every
	// call and never cached: their meta_type dangles once the plugin DLL unloads.
	class PluginComponentHandler
	{
	public:
		PluginComponentHandler() = default;
		~PluginComponentHandler();

		void registerEventHandlers();
		void unregisterEventHandlers();

	private:
		nlohmann::json handleGetTypes() const;
		std::optional<nlohmann::json> handleGet(const events::scene::GetPluginComponentQuery& query) const;
		events::scene::PluginComponentResult handleSetFields(const events::scene::SetPluginComponentFieldsCommand& cmd);
		events::scene::PluginComponentResult handleAdd(const events::scene::AddPluginComponentCommand& cmd);
		events::scene::PluginComponentResult handleRemove(const events::scene::RemovePluginComponentCommand& cmd);
	};
}
