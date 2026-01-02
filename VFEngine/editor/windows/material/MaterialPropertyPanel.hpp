#pragma once
#include <material/MaterialTypes.hpp>
#include <memory>
#include <functional>

namespace editor::graph
{
    class ShaderGraphEditor;
}

namespace editor::materialeditor
{
    class MaterialPropertyPanel
    {
    public:
        using PropertyChangedCallback = std::function<void()>;

        explicit MaterialPropertyPanel() = default;
        ~MaterialPropertyPanel() = default;

        void drawParameterPanel(std::shared_ptr<::material::MaterialData> materialData);

        void drawPropertiesPanel(
            std::shared_ptr<::material::MaterialData> materialData,
            editor::graph::ShaderGraphEditor* graphEditor);

        void setOnPropertyChanged(PropertyChangedCallback callback) { onPropertyChanged = callback; }

    private:
        PropertyChangedCallback onPropertyChanged;

        void notifyChanged();
    };
}
