#pragma once

#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"

namespace mikudancestudio {

// The application owns this world. Diagnostics can inspect the substep
// accumulator without depending on Bullet's private binary layout.
class PhysicsWorld : public btDiscreteDynamicsWorld {
public:
    using btDiscreteDynamicsWorld::btDiscreteDynamicsWorld;

#ifdef MIKUDANCESTUDIO_DIAG
    btScalar SubstepRemainder() const { return m_localTime; }
    void ClearSubstepRemainder() { m_localTime = btScalar(0); }
#endif
};

}  // namespace mikudancestudio
