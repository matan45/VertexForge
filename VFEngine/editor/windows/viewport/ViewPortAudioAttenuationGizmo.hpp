#pragma once

#include "AudioAttenuationGizmoMath.hpp"
#include "data/EntityHandle.hpp"
#include <glm/glm.hpp>

namespace editor
{
    class EditorCamera;
}

namespace windows
{
    class ViewPortPicker;

    class ViewPortAudioAttenuationGizmo
    {
    public:
        void draw(const editor::EditorCamera& camera,
                  ViewPortPicker& picker,
                  glm::vec2 viewportPosition,
                  glm::vec2 viewportSize,
                  bool viewportAvailable,
                  bool isPlayMode,
                  bool windowHovered,
                  bool windowFocused,
                  bool cameraLookActive);

        bool wantsMouseCapture() const
        {
            return dragging || hoveredHandle != audioattenuation::HandleKind::None;
        }

    private:
        bool isToolModeActive() const;
        bool isContextStillEditable(bool viewportAvailable,
                                    bool isPlayMode,
                                    bool windowFocused,
                                    bool cameraLookActive) const;
        bool updateDrag(const editor::EditorCamera& camera,
                        ViewPortPicker& picker,
                        glm::vec2 viewportPosition,
                        glm::vec2 viewportSize);
        void finalizeDrag();
        void cancelDrag();
        void abandonDrag();

        bool dragging = false;
        audioattenuation::HandleKind hoveredHandle = audioattenuation::HandleKind::None;
        audioattenuation::HandleKind draggedHandle = audioattenuation::HandleKind::None;
        services::EntityHandle dragEntity;
        audioattenuation::Distances beforeDistances;
        glm::vec3 dragCenter{0.0f};
        glm::vec3 dragPlaneNormal{0.0f, 0.0f, -1.0f};
        glm::vec3 dragOutwardAxis{1.0f, 0.0f, 0.0f};
    };
}
