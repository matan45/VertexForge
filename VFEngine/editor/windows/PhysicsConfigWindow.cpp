#include "PhysicsConfigWindow.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/PhysicsSettingsEvents.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "print/EditorLogger.hpp"
#include <imgui.h>
#include <vector>
#include <cstring>

namespace windows
{
    void PhysicsConfigWindow::show()
    {
        visible = true;
        if (!settingsLoaded)
        {
            loadFromScene();
        }
    }

    void PhysicsConfigWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 650), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Physics Configuration", &visible))
        {
            drawGravitySection();
            ImGui::Spacing();
            drawSimulationSection();
            ImGui::Spacing();
            drawSleepSection();
            ImGui::Spacing();
            drawLayersSection();
            ImGui::Spacing();
            drawCollisionMatrixSection();
            ImGui::Spacing();
            drawVFXCollisionSection();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Save to Scene", ImVec2(100, 0)))
            {
                saveToScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload", ImVec2(80, 0)))
            {
                loadFromScene();
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply", ImVec2(80, 0)))
            {
                applySettings();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Defaults", ImVec2(100, 0)))
            {
                resetToDefaults();
            }

            if (isDirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Physics settings are saved with the scene file.");
        }
        ImGui::End();
    }

    void PhysicsConfigWindow::drawGravitySection()
    {
        if (ImGui::CollapsingHeader("Gravity", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            ImGui::Text("Gravity Vector");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat3("##GravityVec", &settings.gravity.x, 0.1f, -100.0f, 100.0f, "%.2f"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Global Scale");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##GravityScale", &settings.gravityScale, 0.01f, 0.0f, 10.0f, "%.2f"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            glm::vec3 effective = settings.gravity * settings.gravityScale;
            ImGui::TextDisabled("Effective: (%.2f, %.2f, %.2f)", effective.x, effective.y, effective.z);

            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::drawSimulationSection()
    {
        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            float hz = static_cast<float>(1.0 / settings.fixedTimestep);
            ImGui::Text("Update Rate");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##UpdateRate", &hz, 1.0f, 30.0f, 240.0f, "%.0f Hz"))
            {
                settings.fixedTimestep = 1.0 / static_cast<double>(hz);
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Max Steps Per Frame");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragInt("##MaxSteps", &settings.maxStepsPerFrame, 1, 1, 32))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            float maxAccum = static_cast<float>(settings.maxAccumulator * 1000.0);
            ImGui::Text("Max Accumulator");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##MaxAccum", &maxAccum, 1.0f, 50.0f, 500.0f, "%.0f ms"))
            {
                settings.maxAccumulator = static_cast<double>(maxAccum) / 1000.0;
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::drawSleepSection()
    {
        if (ImGui::CollapsingHeader("Sleep Thresholds"))
        {
            ImGui::Indent();

            ImGui::Text("Linear Velocity Threshold");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##LinSleep", &settings.linearSleepThreshold, 0.001f, 0.0f, 1.0f, "%.3f m/s"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Angular Velocity Threshold");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##AngSleep", &settings.angularSleepThreshold, 0.001f, 0.0f, 1.0f, "%.3f rad/s"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Text("Time to Sleep");
            ImGui::PushItemWidth(-1);
            if (ImGui::DragFloat("##TimeToSleep", &settings.timeToSleep, 0.01f, 0.1f, 5.0f, "%.2f s"))
            {
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::drawLayersSection()
    {
        if (ImGui::CollapsingHeader("Collision Layers", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            for (size_t i = 0; i < settings.layers.size(); ++i)
            {
                auto& layer = settings.layers[i];
                ImGui::PushID(static_cast<int>(i));

                ImGui::Text("[%d]", layer.index);
                ImGui::SameLine();

                if (layer.isBuiltIn)
                {
                    ImGui::TextDisabled("%s (built-in)", layer.name.c_str());
                }
                else
                {
                    char nameBuffer[64];
                    std::strncpy(nameBuffer, layer.name.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    ImGui::PushItemWidth(150);
                    if (ImGui::InputText("##LayerName", nameBuffer, sizeof(nameBuffer)))
                    {
                        layer.name = nameBuffer;
                        isDirty = true;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();

                    if (ImGui::SmallButton("X"))
                    {
                        settings.layers.erase(settings.layers.begin() + i);
                        isDirty = true;
                        ImGui::PopID();
                        break;
                    }
                }

                ImGui::PopID();
            }

            if (settings.layers.size() < types::PhysicsSettings::MAX_LAYERS)
            {
                ImGui::Spacing();
                ImGui::PushItemWidth(150);
                ImGui::InputText("##NewLayerName", newLayerName, sizeof(newLayerName));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Add Layer"))
                {
                    if (strlen(newLayerName) > 0)
                    {
                        uint8_t nextIndex = settings.getNextAvailableLayerIndex();
                        if (nextIndex < types::PhysicsSettings::MAX_LAYERS)
                        {
                            types::CollisionLayer newLayer;
                            newLayer.name = newLayerName;
                            newLayer.index = nextIndex;
                            newLayer.isBuiltIn = false;
                            settings.layers.push_back(newLayer);
                            newLayerName[0] = '\0';
                            isDirty = true;
                        }
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Maximum layers reached (16)");
            }

            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::drawCollisionMatrixSection()
    {
        if (ImGui::CollapsingHeader("Collision Matrix", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent();

            if (settings.layers.empty())
            {
                ImGui::TextDisabled("No collision layers defined");
                ImGui::Unindent();
                return;
            }

            ImGui::BeginTable("CollisionMatrix", static_cast<int>(settings.layers.size()) + 1,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit);

            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            for (const auto& layer : settings.layers)
            {
                ImGui::TableSetupColumn(layer.name.substr(0, 4).c_str(),
                                        ImGuiTableColumnFlags_WidthFixed, 40.0f);
            }
            ImGui::TableHeadersRow();

            for (size_t row = 0; row < settings.layers.size(); ++row)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImGui::Text("%s", settings.layers[row].name.c_str());

                for (size_t col = 0; col < settings.layers.size(); ++col)
                {
                    ImGui::TableNextColumn();

                    uint8_t layerA = settings.layers[row].index;
                    uint8_t layerB = settings.layers[col].index;

                    bool collides = settings.shouldLayersCollide(layerA, layerB);

                    ImGui::PushID(static_cast<int>(row * 16 + col));
                    if (ImGui::Checkbox("##MatrixCell", &collides))
                    {
                        settings.setLayerCollision(layerA, layerB, collides);
                        isDirty = true;
                    }
                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::drawVFXCollisionSection()
    {
        if (ImGui::CollapsingHeader("VFX Particle Colliders"))
        {
            ImGui::Indent();

            ImGui::Text("Max Scene Colliders");
            ImGui::PushItemWidth(-1);
            int maxColliders = static_cast<int>(settings.maxVFXSceneColliders);
            if (ImGui::SliderInt("##MaxVFXColliders", &maxColliders, 1, 128))
            {
                settings.maxVFXSceneColliders = static_cast<uint32_t>(maxColliders);
                isDirty = true;
            }
            ImGui::PopItemWidth();

            ImGui::TextDisabled("Maximum number of scene colliders (Box, Sphere, Capsule)");
            ImGui::TextDisabled("used for VFX particle collision detection.");

            ImGui::Unindent();
        }
    }

    void PhysicsConfigWindow::loadFromScene()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::GetPhysicsSettingsQuery query;
            settings = dispatcher.query(query);
        }
        catch (...)
        {
            settings = types::PhysicsSettings::createDefault();
        }

        if (settings.layers.empty())
        {
            settings = types::PhysicsSettings::createDefault();
        }

        settingsLoaded = true;
        isDirty = false;
    }

    void PhysicsConfigWindow::saveToScene()
    {
        try
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::SetPhysicsSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
            isDirty = false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save physics settings: {}", e.what());
            return;
        }

        applySettings();
    }

    void PhysicsConfigWindow::applySettings()
    {
        try
        {
            events::physics::ApplyPhysicsSettingsCommand cmd;
            cmd.settings = settings;
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.execute(cmd);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to apply physics settings: {}", e.what());
        }
    }

    void PhysicsConfigWindow::resetToDefaults()
    {
        settings = types::PhysicsSettings::createDefault();
        isDirty = true;
    }
}
