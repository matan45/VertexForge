#include "PluginComponentHandler.hpp"
#include "events/EventDispatcher.hpp"
#include "core/PluginContextImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "data/EntityConversion.hpp"
#include "serialization/MetaFieldPatch.hpp"

namespace handlers
{
	namespace
	{
		using events::scene::PluginComponentResult;

		const plugin::MetaComponentBridge* findBridge(const std::string& name)
		{
			for (const auto& bridge : plugin::PluginContextImpl::getAllBridges())
			{
				if (bridge.name == name && bridge.metaType) return &bridge;
			}
			return nullptr;
		}

		PluginComponentResult failure(std::string error)
		{
			PluginComponentResult result;
			result.error = std::move(error);
			return result;
		}

		// Resolves entity + bridge for a mutation; on failure `error` names the problem.
		const plugin::MetaComponentBridge* resolve(const services::EntityHandle& handle, const std::string& type,
		                                           entt::entity& entity, std::string& error)
		{
			auto& reg = scene::EntityRegistry::getRegistry();
			if (!services::internal::isValidHandle(handle, reg))
			{
				error = "invalid entity " + std::to_string(handle.id);
				return nullptr;
			}
			const auto* bridge = findBridge(type);
			if (!bridge)
			{
				error = "unknown plugin component type '" + type + "'";
				return nullptr;
			}
			entity = services::internal::fromHandle(handle);
			return bridge;
		}

		bool isReadOnlyField(const std::string& component, std::string_view field)
		{
			const std::string fieldName(field);
			const auto* attrs = plugin::PluginContextImpl::getFieldAttributes(component.c_str(), fieldName.c_str());
			return attrs && attrs->widget == plugin::inspector::Widget::ReadOnly;
		}
	}

	PluginComponentHandler::~PluginComponentHandler()
	{
		unregisterEventHandlers();
	}

	void PluginComponentHandler::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerQueryHandler<events::scene::GetPluginComponentTypesQuery>(
			[this](const events::scene::GetPluginComponentTypesQuery&) -> nlohmann::json {
				return handleGetTypes();
			});

		dispatcher.registerQueryHandler<events::scene::GetPluginComponentQuery>(
			[this](const events::scene::GetPluginComponentQuery& query) -> std::optional<nlohmann::json> {
				return handleGet(query);
			});

		dispatcher.registerCommandHandler<events::scene::SetPluginComponentFieldsCommand>(
			[this](const events::scene::SetPluginComponentFieldsCommand& cmd) -> PluginComponentResult {
				return handleSetFields(cmd);
			});

		dispatcher.registerCommandHandler<events::scene::AddPluginComponentCommand>(
			[this](const events::scene::AddPluginComponentCommand& cmd) -> PluginComponentResult {
				return handleAdd(cmd);
			});

		dispatcher.registerCommandHandler<events::scene::RemovePluginComponentCommand>(
			[this](const events::scene::RemovePluginComponentCommand& cmd) -> PluginComponentResult {
				return handleRemove(cmd);
			});
	}

	void PluginComponentHandler::unregisterEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unregisterQueryHandler<events::scene::GetPluginComponentTypesQuery>();
		dispatcher.unregisterQueryHandler<events::scene::GetPluginComponentQuery>();
		dispatcher.unregisterCommandHandler<events::scene::SetPluginComponentFieldsCommand>();
		dispatcher.unregisterCommandHandler<events::scene::AddPluginComponentCommand>();
		dispatcher.unregisterCommandHandler<events::scene::RemovePluginComponentCommand>();
	}

	nlohmann::json PluginComponentHandler::handleGetTypes() const
	{
		nlohmann::json types = nlohmann::json::array();
		for (const auto& bridge : plugin::PluginContextImpl::getAllBridges())
		{
			if (!bridge.metaType) continue;

			nlohmann::json fields = nlohmann::json::array();
			for (auto&& [id, member] : bridge.metaType.data())
			{
				const char* name = member.name().data();
				if (!name) continue;

				nlohmann::json field = {
					{"name", name},
					{"type", serialization::meta::metaTypeName(member.type())}
				};
				bool readOnly = member.is_const();
				if (const auto* attrs = plugin::PluginContextImpl::getFieldAttributes(bridge.name.c_str(), name))
				{
					if (attrs->label[0] != '\0') field["label"] = attrs->label;
					if (attrs->widget == plugin::inspector::Widget::ReadOnly) readOnly = true;
					if (attrs->widget == plugin::inspector::Widget::Hidden) field["hidden"] = true;
					if (attrs->hasRange)
					{
						field["min"] = attrs->vmin;
						field["max"] = attrs->vmax;
					}
				}
				if (readOnly) field["readOnly"] = true;
				fields.push_back(std::move(field));
			}

			types.push_back({
				{"name", bridge.name},
				{"plugin", bridge.pluginName},
				{"fields", std::move(fields)}
			});
		}
		return types;
	}

	std::optional<nlohmann::json> PluginComponentHandler::handleGet(const events::scene::GetPluginComponentQuery& query) const
	{
		entt::entity entity = entt::null;
		std::string error;
		const auto* bridge = resolve(query.entity, query.type, entity, error);
		if (!bridge) return std::nullopt;

		auto& reg = scene::EntityRegistry::getRegistry();
		void* ptr = bridge->tryGet(reg, entity);
		if (!ptr) return std::nullopt;

		// Reference into the live component; never copy-construct it (EnTT 4 deep-copies).
		auto instance = bridge->metaType.from_void(ptr);
		if (!instance) return std::nullopt;
		return serialization::meta::serializeFields(instance, bridge->metaType);
	}

	PluginComponentResult PluginComponentHandler::handleSetFields(const events::scene::SetPluginComponentFieldsCommand& cmd)
	{
		entt::entity entity = entt::null;
		std::string error;
		const auto* bridge = resolve(cmd.entity, cmd.type, entity, error);
		if (!bridge) return failure(std::move(error));

		auto& reg = scene::EntityRegistry::getRegistry();
		void* ptr = bridge->tryGet(reg, entity);
		if (!ptr) return failure("entity " + std::to_string(cmd.entity.id) + " has no '" + cmd.type + "' component");

		auto instance = bridge->metaType.from_void(ptr);
		if (!instance) return failure("cannot reflect component '" + cmd.type + "'");

		const std::string component = bridge->name;
		auto patch = serialization::meta::applyFields(instance, bridge->metaType, cmd.fields,
			[&component](std::string_view field) { return isReadOnlyField(component, field); });
		if (!patch.ok) return failure(std::move(patch.error));

		PluginComponentResult result;
		result.ok = true;
		result.value = serialization::meta::serializeFields(instance, bridge->metaType);
		return result;
	}

	PluginComponentResult PluginComponentHandler::handleAdd(const events::scene::AddPluginComponentCommand& cmd)
	{
		entt::entity entity = entt::null;
		std::string error;
		const auto* bridge = resolve(cmd.entity, cmd.type, entity, error);
		if (!bridge) return failure(std::move(error));

		auto& reg = scene::EntityRegistry::getRegistry();
		if (bridge->has(reg, entity))
			return failure("entity " + std::to_string(cmd.entity.id) + " already has a '" + cmd.type + "' component");

		bridge->emplace(reg, entity);
		void* ptr = bridge->tryGet(reg, entity);
		auto instance = ptr ? bridge->metaType.from_void(ptr) : entt::meta_any{};
		if (!instance)
		{
			bridge->remove(reg, entity);
			return failure("cannot create component '" + cmd.type + "'");
		}

		if (!cmd.fields.is_null())
		{
			const std::string component = bridge->name;
			auto patch = serialization::meta::applyFields(instance, bridge->metaType, cmd.fields,
				[&component](std::string_view field) { return isReadOnlyField(component, field); });
			if (!patch.ok)
			{
				bridge->remove(reg, entity);
				return failure(std::move(patch.error));
			}
		}

		PluginComponentResult result;
		result.ok = true;
		result.value = serialization::meta::serializeFields(instance, bridge->metaType);
		return result;
	}

	PluginComponentResult PluginComponentHandler::handleRemove(const events::scene::RemovePluginComponentCommand& cmd)
	{
		entt::entity entity = entt::null;
		std::string error;
		const auto* bridge = resolve(cmd.entity, cmd.type, entity, error);
		if (!bridge) return failure(std::move(error));

		auto& reg = scene::EntityRegistry::getRegistry();
		void* ptr = bridge->tryGet(reg, entity);
		if (!ptr) return failure("entity " + std::to_string(cmd.entity.id) + " has no '" + cmd.type + "' component");

		PluginComponentResult result;
		if (auto instance = bridge->metaType.from_void(ptr))
			result.value = serialization::meta::serializeFields(instance, bridge->metaType);
		else
			result.value = nlohmann::json::object();

		bridge->remove(reg, entity);
		result.ok = true;
		return result;
	}
}
