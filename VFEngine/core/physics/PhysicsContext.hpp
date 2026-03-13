#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include "PhysicsLayers.hpp"

namespace core::physics
{
    struct PhysicsContext
    {
        JPH::PhysicsSystem* physicsSystem = nullptr;
        JPH::TempAllocatorImpl* tempAllocator = nullptr;
        ObjectVsBroadPhaseLayerFilterImpl* broadPhaseFilter = nullptr;
        ObjectLayerPairFilterImpl* objectLayerFilter = nullptr;

        JPH::BodyInterface& getBodyInterface() const
        {
            return physicsSystem->GetBodyInterface();
        }

        const JPH::BodyLockInterface& getBodyLockInterface() const
        {
            return physicsSystem->GetBodyLockInterface();
        }

        const JPH::NarrowPhaseQuery& getNarrowPhaseQuery() const
        {
            return physicsSystem->GetNarrowPhaseQuery();
        }
    };

    inline void removeAndDestroyBody(JPH::BodyInterface& bi, JPH::BodyID bodyId)
    {
        if (bi.IsAdded(bodyId)) bi.RemoveBody(bodyId);
        bi.DestroyBody(bodyId);
    }
}
