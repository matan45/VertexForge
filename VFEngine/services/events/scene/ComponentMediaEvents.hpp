#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>

namespace events::scene {

    // ============================================
    // Camera Component Events
    // ============================================

    struct AddCameraComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddCameraComponent"; }
    };

    struct RemoveCameraComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveCameraComponent"; }
    };

    struct SetCameraDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::CameraData cameraData;

        std::string_view getName() const override { return "SetCameraData"; }
    };

    struct HasCameraComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasCameraComponent"; }
    };

    struct GetCameraDataQuery : IQuery<std::optional<services::CameraData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetCameraData"; }
    };

    // ============================================
    // Mesh Component Events
    // ============================================

    struct AddMeshComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddMeshComponent"; }
    };

    struct RemoveMeshComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveMeshComponent"; }
    };

    struct SetMeshDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::MeshData meshData;

        std::string_view getName() const override { return "SetMeshData"; }
    };

    struct HasMeshComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasMeshComponent"; }
    };

    struct GetMeshDataQuery : IQuery<std::optional<services::MeshData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetMeshData"; }
    };

    // ============================================
    // IBL Component Events
    // ============================================

    struct SetIBLDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::IBLData iblData;

        std::string_view getName() const override { return "SetIBLData"; }
    };

    struct RemoveIBLComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveIBLComponent"; }
    };

    struct HasIBLComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasIBLComponent"; }
    };

    struct GetIBLDataQuery : IQuery<std::optional<services::IBLData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetIBLData"; }
    };

    // ============================================
    // Billboard Component Events
    // ============================================

    struct AddBillboardComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddBillboardComponent"; }
    };

    struct RemoveBillboardComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveBillboardComponent"; }
    };

    struct SetBillboardDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::BillboardData billboardData;

        std::string_view getName() const override { return "SetBillboardData"; }
    };

    struct HasBillboardComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasBillboardComponent"; }
    };

    struct GetBillboardDataQuery : IQuery<std::optional<services::BillboardData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBillboardData"; }
    };

    // ============================================
    // Text Component Events
    // ============================================

    struct AddTextComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddTextComponent"; }
    };

    struct RemoveTextComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveTextComponent"; }
    };

    struct SetTextDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::TextData textData;

        std::string_view getName() const override { return "SetTextData"; }
    };

    struct HasTextComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasTextComponent"; }
    };

    struct GetTextDataQuery : IQuery<std::optional<services::TextData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTextData"; }
    };

    // ============================================
    // VFX Component Events
    // ============================================

    struct AddVFXComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddVFXComponent"; }
    };

    struct RemoveVFXComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveVFXComponent"; }
    };

    struct SetVFXDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::VFXData vfxData;

        std::string_view getName() const override { return "SetVFXData"; }
    };

    struct HasVFXComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasVFXComponent"; }
    };

    struct GetVFXDataQuery : IQuery<std::optional<services::VFXData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetVFXData"; }
    };

    // ============================================
    // RenderTexture Component Events
    // ============================================

    struct AddRenderTextureComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddRenderTextureComponent"; }
    };

    struct RemoveRenderTextureComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveRenderTextureComponent"; }
    };

    struct SetRenderTextureDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::RenderTextureData renderTextureData;

        std::string_view getName() const override { return "SetRenderTextureData"; }
    };

    struct HasRenderTextureComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasRenderTextureComponent"; }
    };

    struct GetRenderTextureDataQuery : IQuery<std::optional<services::RenderTextureData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetRenderTextureData"; }
    };

    // ============================================
    // Mesh Data Changed Notification
    // ============================================

    struct MeshDataChangedNotification : INotification {
        services::EntityHandle entity;
        std::string meshPath;
        std::string animatorPath;

        std::string_view getName() const override { return "MeshDataChanged"; }
    };

}
