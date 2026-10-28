#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btGeneric6DofSpringConstraint.h"

#include <cstdio>
#include <memory>
#include <windows.h>

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    btRigidBody::btRigidBodyConstructionInfo info(0, nullptr, nullptr);
    btRigidBody first(info), second(info);
    btTransform frame;
    frame.setIdentity();
    // Allocation is compiled here; the constructor installs Bullet's vtable,
    // whose deleting destructor must use the same allocation convention.
    for (int i = 0; i < 32; ++i) {
        std::unique_ptr<btTypedConstraint> constraint(
            new btGeneric6DofSpringConstraint(first, second, frame, frame, true));
        constraint->setUserConstraintId(i);
        if (constraint->getUserConstraintId() != i)
            return 1;
    }
    std::puts("Bullet constraint allocation and virtual deletion passed");
    return 0;
}
