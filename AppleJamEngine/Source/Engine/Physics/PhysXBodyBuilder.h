#pragma once

#include "Physics/PhysicsTypes.h"

namespace physx
{
    class PxPhysics;
    class PxCooking;
    class PxMaterial;
    class PxRigidActor;
    class PxRigidDynamic;
    class PxShape;
}

class FPhysXBodyBuilder
{
public:
    static physx::PxRigidActor* CreateRigidActor(
        physx::PxPhysics*        Physics,
        const FBodyCreationDesc& Desc
    );
    static physx::PxShape* CreateShape(
        physx::PxPhysics*        Physics,
        physx::PxCooking*        Cooking,
        physx::PxMaterial*       DefaultMaterial,
        const FPhysicsShapeDesc& Desc
    );
    static void ApplyShapeProperties(
        physx::PxShape*          Shape,
        const FPhysicsShapeDesc& Desc
    );
    static void ApplyBodyProperties(
        physx::PxRigidActor*     Actor,
        const FBodyCreationDesc& Desc
    );
    static void UpdateMassAndInertia(
        physx::PxRigidActor*     Actor,
        const FBodyCreationDesc& Desc
    );
};
