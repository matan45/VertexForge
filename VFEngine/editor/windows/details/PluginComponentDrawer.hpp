#pragma once
#include "data/EntityHandle.hpp"
#include "components/PluginPropertyTypes.hpp"
#include <nlohmann/json.hpp>
#include <vector>

namespace windows::details
{
    class PluginComponentDrawer
    {
    public:
        void draw(services::EntityHandle handle);

    private:
        void drawSingleComponent(services::EntityHandle handle,
                                 const std::string& qualifiedName);

        bool drawAutoInspector(const std::vector<components::plugin::PropertyDescriptor>& properties,
                               nlohmann::json& data);
    };
}
