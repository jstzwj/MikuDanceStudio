/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2006 Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include "btSphereSphereCollisionAlgorithm.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "BulletCollision/CollisionDispatch/btCollisionObject.h"

btSphereSphereCollisionAlgorithm::btSphereSphereCollisionAlgorithm(btPersistentManifold* mf,const btCollisionAlgorithmConstructionInfo& ci,btCollisionObject* col0,btCollisionObject* col1)
: btActivatingCollisionAlgorithm(ci,col0,col1),
m_ownManifold(false),
m_manifoldPtr(mf)
{
	if (!m_manifoldPtr)
	{
		m_manifoldPtr = m_dispatcher->getNewManifold(col0,col1);
		m_ownManifold = true;
	}
}

btSphereSphereCollisionAlgorithm::~btSphereSphereCollisionAlgorithm()
{
	if (m_ownManifold)
	{
		if (m_manifoldPtr)
			m_dispatcher->releaseManifold(m_manifoldPtr);
	}
}

void btSphereSphereCollisionAlgorithm::processCollision (btCollisionObject* col0,btCollisionObject* col1,const btDispatcherInfo& dispatchInfo,btManifoldResult* resultOut)
{
	(void)dispatchInfo;

	if (!m_manifoldPtr)
		return;

	resultOut->setPersistentManifold(m_manifoldPtr);

	btSphereShape* sphere0 = (btSphereShape*)col0->getCollisionShape();
	btSphereShape* sphere1 = (btSphereShape*)col1->getCollisionShape();

	btVector3 diff = col0->getWorldTransform().getOrigin()-  col1->getWorldTransform().getOrigin();
#if defined(_MSC_VER) && defined(_M_IX86)
	// MMD 0x4DFA33..0x4DFA88: the whole length chain is x87 extended -
	// the origins are SUBTRACTED on the FPU stack (fsub of two floats is
	// exact in extended), squared and folded x*x + (y*y + z*z) without a
	// single rounding, fsqrt in extended, and ONE fstp to float at the end.
	// The stock SSE path stores the rounded float diff first and rounds
	// (x*x+y*y)+z*z at every step.  Double arithmetic on the exact
	// difference reproduces the extended chain bit-for-bit.
	btScalar len;
	{
		const btVector3& o0 = col0->getWorldTransform().getOrigin();
		const btVector3& o1 = col1->getWorldTransform().getOrigin();
		const double dx = (double)o0.getX() - (double)o1.getX();
		const double dy = (double)o0.getY() - (double)o1.getY();
		const double dz = (double)o0.getZ() - (double)o1.getZ();
		len = static_cast<btScalar>(
		    sqrt(dx * dx + (dy * dy + dz * dz)));
	}
#else
	btScalar len = diff.length();
#endif
	btScalar radius0 = sphere0->getRadius();
	btScalar radius1 = sphere1->getRadius();

#ifdef CLEAR_MANIFOLD
	m_manifoldPtr->clearManifold(); //don't do this, it disables warmstarting
#endif

	///iff distance positive, don't generate a new contact
	if ( len > (radius0+radius1))
	{
#ifndef CLEAR_MANIFOLD
		resultOut->refreshContactPoints();
#endif //CLEAR_MANIFOLD
		return;
	}
	///distance (negative means penetration)
	btScalar dist = len - (radius0+radius1);

	btVector3 normalOnSurfaceB(1,0,0);
	if (len > SIMD_EPSILON)
	{
		normalOnSurfaceB = diff / len;
	}

	///point on A (worldspace)
	///btVector3 pos0 = col0->getWorldTransform().getOrigin() - radius0 * normalOnSurfaceB;
	///point on B (worldspace)
	btVector3 pos1 = col1->getWorldTransform().getOrigin() + radius1* normalOnSurfaceB;

	/// report a contact. internally this will be kept persistent, and contact reduction is done
	
	
	resultOut->addContactPoint(normalOnSurfaceB,pos1,dist);

#ifndef CLEAR_MANIFOLD
	resultOut->refreshContactPoints();
#endif //CLEAR_MANIFOLD

}

btScalar btSphereSphereCollisionAlgorithm::calculateTimeOfImpact(btCollisionObject* col0,btCollisionObject* col1,const btDispatcherInfo& dispatchInfo,btManifoldResult* resultOut)
{
	(void)col0;
	(void)col1;
	(void)dispatchInfo;
	(void)resultOut;

	//not yet
	return btScalar(1.);
}
