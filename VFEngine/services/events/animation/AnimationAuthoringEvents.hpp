#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/AnimationAuthoringData.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1656 animation authoring (MCP P7).
//
// Asset-level access the MCP tools need without linking Animation.dll: .vfAnimator
// load/save, a mesh's skeleton/sockets/IK chains, clip summaries, retarget bindings,
// and the edit-mode preview on a scene entity. Handled by core::AnimationAuthoringAdapter
// (Editor only). Paths are absolute engine paths. MAIN THREAD ONLY: the MCP drain runs
// at the top of the frame with the render thread idle.

namespace events::animationAuthoring
{
    // Reads a .vfAnimator from disk (never a cache).
    struct LoadAnimatorAssetQuery : IQuery<services::AnimatorAssetLoadResult>
    {
        std::string path;

        std::string_view getName() const override { return "LoadAnimatorAsset"; }
    };

    // Writes a .vfAnimator, then publishes AssetSavedNotification: the asset database
    // registers the GUID/.vfmeta, live animators using the file reload, and an open
    // Animator Editor reloads (or shows a conflict when it has unsaved edits).
    // Refuses when `createNew` is set and the file exists, or when `expectedContentHash`
    // is set and the file's current bytes hash differently.
    struct SaveAnimatorAssetCommand : ICommand<services::AnimatorAssetSaveResult>
    {
        std::string path;
        animator::AnimatorData data;
        std::optional<uint64_t> expectedContentHash;
        bool createNew = false;

        std::string_view getName() const override { return "SaveAnimatorAsset"; }
    };

    // Skeleton, sockets and authored IK chains of a .vfMesh, read from disk (never the
    // runtime cache, which can be stale).
    struct GetMeshSkeletonQuery : IQuery<services::MeshSkeletonInfo>
    {
        std::string meshPath;

        std::string_view getName() const override { return "GetMeshSkeleton"; }
    };

    // Header and channel names of each .vfAnim (keys are not read). One result per path,
    // in order; a file that cannot be read reports success = false.
    struct GetAnimationClipSummariesQuery : IQuery<std::vector<services::AnimationClipSummary>>
    {
        std::vector<std::string> paths;

        std::string_view getName() const override { return "GetAnimationClipSummaries"; }
    };

    // A .vfretarget map with both of its .vfrig profiles.
    struct GetRetargetBindingQuery : IQuery<services::RetargetBindingInfo>
    {
        std::string retargetPath;

        std::string_view getName() const override { return "GetRetargetBinding"; }
    };

    // Plays (or poses) a state or clip on a scene entity in edit mode, on a preview stack
    // separate from the entity's animator. The entity needs an Animator; refused in Play
    // mode. Replaces the entity's current preview.
    struct StartAnimationEditPreviewCommand : ICommand<services::AnimationEditPreviewResult>
    {
        services::EntityHandle entity;
        services::AnimationEditPreviewRequest request;

        std::string_view getName() const override { return "StartAnimationEditPreview"; }
    };

    // Stops the entity's preview; an invalid handle stops every preview. Returns whether
    // a preview was stopped.
    struct StopAnimationEditPreviewCommand : ICommand<bool>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "StopAnimationEditPreview"; }
    };

    struct GetAnimationEditPreviewQuery : IQuery<std::optional<services::AnimationEditPreviewStatus>>
    {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetAnimationEditPreview"; }
    };
}
