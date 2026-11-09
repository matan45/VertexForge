#include "PluginPostProcessEffectHandler.hpp"
#include "print/Log.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/PluginPostProcessEffectEvents.hpp"
#include "core/PluginManager.hpp"
#include "core/PluginEventBus.hpp"
#include <nlohmann/json.hpp>

namespace handlers
{
	namespace
	{
		using events::editor::PluginPostProcessEffectInfo;

		std::vector<PluginPostProcessEffectInfo> listEffects()
		{
			std::vector<PluginPostProcessEffectInfo> effects;
			const auto* manager = plugin::PluginManager::getActive();
			if (!manager) return effects;

			for (const auto& loaded : manager->getLoadedPlugins())
			{
				if (!loaded.context) continue;
				for (const auto& effect : loaded.context->getManagedPostProcessEffects())
				{
					PluginPostProcessEffectInfo info;
					info.id = effect.handle.id;
					info.plugin = loaded.context->getPluginName();
					info.name = effect.debugName;
					info.priority = effect.priority;
					info.requestedEnabled = effect.requestedEnabled;
					info.pluginActive = loaded.context->isActive();
					effects.push_back(std::move(info));
				}
			}
			return effects;
		}

		bool setEffectEnabled(uint64_t id, bool enabled)
		{
			auto* manager = plugin::PluginManager::getActiveMutable();
			if (!manager) return false;

			// Resolve the owner first: the publish below runs plugin code, which may register or
			// unregister effects, so it must not run while an effect list is being walked.
			plugin::PluginContextImpl* owner = nullptr;
			std::string effectName;
			for (const auto& loaded : manager->getLoadedPlugins())
			{
				if (!loaded.context) continue;
				for (const auto& effect : loaded.context->getManagedPostProcessEffects())
				{
					if (effect.handle.id != id) continue;
					owner = loaded.context.get();
					effectName = effect.debugName;
					break;
				}
				if (owner) break;
			}
			if (!owner) return false;

			// The plugin's own path: records the request, which renders only while the plugin is active.
			owner->setPostProcessEffectEnabled(plugin::PostProcessEffectHandle{id}, enabled);

			const nlohmann::json payload = {
				{"id", id},
				{"plugin", owner->getPluginName()},
				{"name", effectName},
				{"enabled", enabled}
			};
			try
			{
				// Synchronous: the owning plugin keeps its own toggle (and UI) in step.
				plugin::PluginEventBus::instance().publish("vf.postprocess.effectEnabled", payload);
			}
			catch (const std::exception& e)
			{
				vfLogWarning("[Plugin] A 'vf.postprocess.effectEnabled' subscriber threw: {}", e.what());
			}
			return true;
		}
	}

	PluginPostProcessEffectHandler::~PluginPostProcessEffectHandler()
	{
		unregisterEventHandlers();
	}

	void PluginPostProcessEffectHandler::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerQueryHandler<events::editor::ListPluginPostProcessEffectsQuery>(
			[](const events::editor::ListPluginPostProcessEffectsQuery&) -> std::vector<PluginPostProcessEffectInfo> {
				return listEffects();
			});

		dispatcher.registerCommandHandler<events::editor::SetPluginPostProcessEffectEnabledCommand>(
			[](const events::editor::SetPluginPostProcessEffectEnabledCommand& cmd) -> bool {
				return setEffectEnabled(cmd.id, cmd.enabled);
			});
	}

	void PluginPostProcessEffectHandler::unregisterEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unregisterQueryHandler<events::editor::ListPluginPostProcessEffectsQuery>();
		dispatcher.unregisterCommandHandler<events::editor::SetPluginPostProcessEffectEnabledCommand>();
	}
}
