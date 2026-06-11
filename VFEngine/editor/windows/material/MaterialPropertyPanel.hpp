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

        // Structural changes (expose/rename, node values, links) — needs a shader recompile
        void setOnPropertyChanged(PropertyChangedCallback callback) { onPropertyChanged = callback; }

        // Exposed-parameter value tweaks — flow through the parameter UBO, no recompile
        void setOnParameterValueChanged(PropertyChangedCallback callback) { onParameterValueChanged = callback; }

    private:
        PropertyChangedCallback onPropertyChanged;
        PropertyChangedCallback onParameterValueChanged;

        void notifyChanged();
        void notifyValueChanged();

        void drawExposeSection(::material::ShaderNode& node, bool& structuralChange);
    };
}
