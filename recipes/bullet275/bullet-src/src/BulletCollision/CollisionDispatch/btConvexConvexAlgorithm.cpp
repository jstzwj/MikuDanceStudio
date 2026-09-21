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

///Specialized capsule-capsule collision algorithm has been added for Bullet 2.75 release to increase ragdoll performance
///If you experience problems with capsule-capsule collision, try to define BT_DISABLE_CAPSULE_CAPSULE_COLLIDER and report it in the Bullet forums
///with reproduction case
//define BT_DISABLE_CAPSULE_CAPSULE_COLLIDER 1

#include "btConvexConvexAlgorithm.h"

//#include <stdio.h>
#include "BulletCollision/NarrowPhaseCollision/btDiscreteCollisionDetectorInterface.h"
#include "BulletCollision/BroadphaseCollision/btBroadphaseInterface.h"
#include "BulletCollision/CollisionDispatch/btCollisionObject.h"
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"


#include "BulletCollision/NarrowPhaseCollision/btGjkPairDetector.h"
#include "BulletCollision/BroadphaseCollision/btBroadphaseProxy.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"
#include "BulletCollision/CollisionDispatch/btManifoldResult.h"

#include "BulletCollision/NarrowPhaseCollision/btConvexPenetrationDepthSolver.h"
#include "BulletCollision/NarrowPhaseCollision/btContinuousConvexCollision.h"
#include "BulletCollision/NarrowPhaseCollision/btSubSimplexConvexCast.h"
#include "BulletCollision/NarrowPhaseCollision/btGjkConvexCast.h"



#include "BulletCollision/NarrowPhaseCollision/btVoronoiSimplexSolver.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"

#include "BulletCollision/NarrowPhaseCollision/btMinkowskiPenetrationDepthSolver.h"

#include "BulletCollision/NarrowPhaseCollision/btGjkEpa2.h"
#include "BulletCollision/NarrowPhaseCollision/btGjkEpaPenetrationDepthSolver.h"



///////////



// MMD 0x4E18B0 (capsule-capsule fast path): the distance is computed as
// (float)(sqrt(lenSqr) - radiusA - radiusB) with the square root and both
// subtractions kept extended on the x87 stack.
static __declspec(noinline) btScalar mmdCapsuleDistance(btScalar lenSqr,
                                                       btScalar radiusA,
                                                       btScalar radiusB)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	btScalar result;
	__asm
	{
		fld dword ptr [lenSqr]
		fsqrt
		fsub dword ptr [radiusA]
		fsub dword ptr [radiusB]
		fstp dword ptr [result]
	}
	return result;
#else
	return btSqrt(lenSqr) - radiusA - radiusB;
#endif
}

// normalOnB = ptsVector * -(1/sqrt(lenSqr)), reciprocal extended and each
// component rounded once (MMD 0x4E18B0).
static __declspec(noinline) void mmdCapsuleNormal(btVector3& normal,
                                                 const btVector3& pts,
                                                 btScalar lenSqr)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	btScalar* n = &normal.m_floats[0];
	const btScalar* p = &pts.m_floats[0];
	__asm
	{
		mov eax, n
		mov edx, p
		fld dword ptr [lenSqr]
		fsqrt
		fld1
		fdivrp st(1), st
		fchs
		fld dword ptr [edx]
		fmul st, st(1)
		fstp dword ptr [eax]
		fld dword ptr [edx+4]
		fmul st, st(1)
		fstp dword ptr [eax+4]
		fld dword ptr [edx+8]
		fmul st, st(1)
		fstp dword ptr [eax+8]
		fstp st(0)
	}
#else
	normal = pts * -btRecipSqrt(lenSqr);
#endif
}

// MMD's plane-space fallback branches: inv = 1/sqrt(a*a + b*b) stays
// extended while the two components are scaled once.
static __declspec(noinline) void mmdCapsulePlaneSpace(btVector3& normal,
                                                     btScalar a, btScalar b,
                                                     bool zBranch)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	btScalar* n = &normal.m_floats[0];
	int useZ = zBranch;
	__asm
	{
		mov eax, n
		fld dword ptr [a]
		fmul st, st
		fld dword ptr [b]
		fmul st, st
		faddp st(1), st
		fsqrt
		fld1
		fdivrp st(1), st
		cmp useZ, 0
		jz  else_branch
		fld dword ptr [b]
		fmul st, st(1)
		fstp dword ptr [eax+8]
		fld dword ptr [a]
		fmul st, st(1)
		fchs
		fstp dword ptr [eax+4]
		xorps xmm0, xmm0
		movss dword ptr [eax], xmm0
		fstp st(0)
		jmp done
else_branch:
		fld dword ptr [a]
		fmul st, st(1)
		fstp dword ptr [eax+4]
		fld dword ptr [b]
		fmul st, st(1)
		fchs
		fstp dword ptr [eax]
		xorps xmm0, xmm0
		movss dword ptr [eax+8], xmm0
		fstp st(0)
done:
	}
#else
	const btScalar inv = btScalar(1.)/btSqrt(a*a + b*b);
	if (zBranch)
		normal.setValue(0, -(a*inv), inv*b);
	else
		normal.setValue(-(b*inv), a*inv, 0);
#endif
}

static SIMD_FORCE_INLINE void segmentsClosestPoints(
	btVector3& ptsVector,
	btVector3& offsetA,
	btVector3& offsetB,
	btScalar& tA, btScalar& tB,
	const btVector3& translation,
	const btVector3& dirA, btScalar hlenA,
	const btVector3& dirB, btScalar hlenB )
{
	// compute the parameters of the closest points on each line segment

	// VC9 (MMD 0x4E18B0): dirA_dot_dirB and dirA_dot_trans are grouped
	// (z + x) + y, dirB_dot_trans is grouped (z + y) + x.
	btScalar dirA_dot_dirB = (dirB.z()*dirA.z() + dirB.x()*dirA.x())
		+ dirB.y()*dirA.y();
	btScalar dirA_dot_trans = (translation.z()*dirA.z()
		+ translation.x()*dirA.x()) + translation.y()*dirA.y();
	btScalar dirB_dot_trans = (translation.z()*dirB.z()
		+ translation.y()*dirB.y()) + translation.x()*dirB.x();

	btScalar denom = 1.0f - dirA_dot_dirB * dirA_dot_dirB;

	if ( denom == 0.0f ) {
		tA = 0.0f;
	} else {
		tA = ( dirA_dot_trans - dirB_dot_trans * dirA_dot_dirB ) / denom;
		if ( tA < -hlenA )
			tA = -hlenA;
		else if ( tA > hlenA )
			tA = hlenA;
	}

	tB = tA * dirA_dot_dirB - dirB_dot_trans;

	if ( tB < -hlenB ) {
		tB = -hlenB;
		tA = tB * dirA_dot_dirB + dirA_dot_trans;

		if ( tA < -hlenA )
			tA = -hlenA;
		else if ( tA > hlenA )
			tA = hlenA;
	} else if ( tB > hlenB ) {
		tB = hlenB;
		tA = tB * dirA_dot_dirB + dirA_dot_trans;

		if ( tA < -hlenA )
			tA = -hlenA;
		else if ( tA > hlenA )
			tA = hlenA;
	}

	// compute the closest points relative to segment centers.

	offsetA = dirA * tA;
	offsetB = dirB * tB;

	ptsVector = translation - offsetA + offsetB;
}


static SIMD_FORCE_INLINE btScalar capsuleCapsuleDistance(
	btVector3& normalOnB,
	btVector3& pointOnB,
	btScalar capsuleLengthA,
	btScalar	capsuleRadiusA,
	btScalar capsuleLengthB,
	btScalar	capsuleRadiusB,
	int capsuleAxisA,
	int capsuleAxisB,
	const btTransform& transformA,
	const btTransform& transformB,
	btScalar distanceThreshold )
{
	btVector3 directionA = transformA.getBasis().getColumn(capsuleAxisA);
	btVector3 translationA = transformA.getOrigin();
	btVector3 directionB = transformB.getBasis().getColumn(capsuleAxisB);
	btVector3 translationB = transformB.getOrigin();

	// translation between centers

	btVector3 translation = translationB - translationA;

	// compute the closest points of the capsule line segments

	btVector3 ptsVector;           // the vector between the closest points
	
	btVector3 offsetA, offsetB;    // offsets from segment centers to their closest points
	btScalar tA, tB;              // parameters on line segment

	segmentsClosestPoints( ptsVector, offsetA, offsetB, tA, tB, translation,
						   directionA, capsuleLengthA, directionB, capsuleLengthB );

	// VC9 (MMD 0x4E18B0): lenSqr is ((z*z + y*y) + x*x); the distance keeps
	// the sqrt and both radius subtractions extended.
	const btScalar lenSqr = (ptsVector.z()*ptsVector.z()
		+ ptsVector.y()*ptsVector.y()) + ptsVector.x()*ptsVector.x();
	btScalar distance = mmdCapsuleDistance(lenSqr, capsuleRadiusA,
		capsuleRadiusB);

	if ( distance > distanceThreshold )
		return distance;

	if (lenSqr<= (SIMD_EPSILON*SIMD_EPSILON))
	{
		//degenerate case where 2 capsules are likely at the same location: take a vector tangential to 'directionA'
		if (btFabs(directionA.z()) > btScalar(0.70710677))
		{
			mmdCapsulePlaneSpace(normalOnB, directionA.z(), directionA.y(), true);
		}
		else
		{
			mmdCapsulePlaneSpace(normalOnB, directionA.x(), directionA.y(), false);
		}
	} else
	{
		// compute the contact normal
		mmdCapsuleNormal(normalOnB, ptsVector, lenSqr);
	}
	pointOnB = transformB.getOrigin()+offsetB + normalOnB * capsuleRadiusB;

	return distance;
}







//////////





btConvexConvexAlgorithm::CreateFunc::CreateFunc(btSimplexSolverInterface*			simplexSolver, btConvexPenetrationDepthSolver* pdSolver)
{
	m_numPerturbationIterations = 0;
	m_minimumPointsPerturbationThreshold = 3;
	m_simplexSolver = simplexSolver;
	m_pdSolver = pdSolver;
}

btConvexConvexAlgorithm::CreateFunc::~CreateFunc() 
{ 
}

btConvexConvexAlgorithm::btConvexConvexAlgorithm(btPersistentManifold* mf,const btCollisionAlgorithmConstructionInfo& ci,btCollisionObject* body0,btCollisionObject* body1,btSimplexSolverInterface* simplexSolver, btConvexPenetrationDepthSolver* pdSolver,int numPerturbationIterations, int minimumPointsPerturbationThreshold)
: btActivatingCollisionAlgorithm(ci,body0,body1),
m_simplexSolver(simplexSolver),
m_pdSolver(pdSolver),
m_ownManifold (false),
m_manifoldPtr(mf),
m_lowLevelOfDetail(false),
#ifdef USE_SEPDISTANCE_UTIL2
m_sepDistance((static_cast<btConvexShape*>(body0->getCollisionShape()))->getAngularMotionDisc(),
			  (static_cast<btConvexShape*>(body1->getCollisionShape()))->getAngularMotionDisc()),
#endif
m_numPerturbationIterations(numPerturbationIterations),
m_minimumPointsPerturbationThreshold(minimumPointsPerturbationThreshold)
{
	(void)body0;
	(void)body1;
}




btConvexConvexAlgorithm::~btConvexConvexAlgorithm()
{
	if (m_ownManifold)
	{
		if (m_manifoldPtr)
			m_dispatcher->releaseManifold(m_manifoldPtr);
	}
}

void	btConvexConvexAlgorithm ::setLowLevelOfDetail(bool useLowLevel)
{
	m_lowLevelOfDetail = useLowLevel;
}


struct btPerturbedContactResult : public btManifoldResult
{
	btManifoldResult* m_originalManifoldResult;
	btTransform m_transformA;
	btTransform m_transformB;
	btTransform	m_unPerturbedTransform;
	bool	m_perturbA;
	btIDebugDraw*	m_debugDrawer;


	btPerturbedContactResult(btManifoldResult* originalResult,const btTransform& transformA,const btTransform& transformB,const btTransform& unPerturbedTransform,bool perturbA,btIDebugDraw* debugDrawer)
		:m_originalManifoldResult(originalResult),
		m_transformA(transformA),
		m_transformB(transformB),
		m_perturbA(perturbA),
		m_unPerturbedTransform(unPerturbedTransform),
		m_debugDrawer(debugDrawer)
	{
	}
	virtual ~ btPerturbedContactResult()
	{
	}

	virtual void addContactPoint(const btVector3& normalOnBInWorld,const btVector3& pointInWorld,btScalar orgDepth)
	{
		btVector3 endPt,startPt;
		btScalar newDepth;
		btVector3 newNormal;

		if (m_perturbA)
		{
			btVector3 endPtOrg = pointInWorld + normalOnBInWorld*orgDepth;
			endPt = (m_unPerturbedTransform*m_transformA.inverse())(endPtOrg);
			newDepth = (endPt -  pointInWorld).dot(normalOnBInWorld);
			startPt = endPt+normalOnBInWorld*newDepth;
		} else
		{
			endPt = pointInWorld + normalOnBInWorld*orgDepth;
			startPt = (m_unPerturbedTransform*m_transformB.inverse())(pointInWorld);
			newDepth = (endPt -  startPt).dot(normalOnBInWorld);
			
		}

//#define DEBUG_CONTACTS 1
#ifdef DEBUG_CONTACTS
		m_debugDrawer->drawLine(startPt,endPt,btVector3(1,0,0));
		m_debugDrawer->drawSphere(startPt,0.05,btVector3(0,1,0));
		m_debugDrawer->drawSphere(endPt,0.05,btVector3(0,0,1));
#endif //DEBUG_CONTACTS

		
		m_originalManifoldResult->addContactPoint(normalOnBInWorld,startPt,newDepth);
	}

};

extern btScalar gContactBreakingThreshold;


//
// Convex-Convex collision algorithm
//
void btConvexConvexAlgorithm ::processCollision (btCollisionObject* body0,btCollisionObject* body1,const btDispatcherInfo& dispatchInfo,btManifoldResult* resultOut)
{

	if (!m_manifoldPtr)
	{
		//swapped?
		m_manifoldPtr = m_dispatcher->getNewManifold(body0,body1);
		m_ownManifold = true;
	}
	resultOut->setPersistentManifold(m_manifoldPtr);

	//comment-out next line to test multi-contact generation
	//resultOut->getPersistentManifold()->clearManifold();
	

	btConvexShape* min0 = static_cast<btConvexShape*>(body0->getCollisionShape());
	btConvexShape* min1 = static_cast<btConvexShape*>(body1->getCollisionShape());

	btVector3  normalOnB;
		btVector3  pointOnBWorld;
#ifndef BT_DISABLE_CAPSULE_CAPSULE_COLLIDER
	if ((min0->getShapeType() == CAPSULE_SHAPE_PROXYTYPE) && (min1->getShapeType() == CAPSULE_SHAPE_PROXYTYPE))
	{
		btCapsuleShape* capsuleA = (btCapsuleShape*) min0;
		btCapsuleShape* capsuleB = (btCapsuleShape*) min1;
		btVector3 localScalingA = capsuleA->getLocalScaling();
		btVector3 localScalingB = capsuleB->getLocalScaling();
		
		btScalar threshold = m_manifoldPtr->getContactBreakingThreshold();

		btScalar dist = capsuleCapsuleDistance(normalOnB,	pointOnBWorld,capsuleA->getHalfHeight(),capsuleA->getRadius(),
			capsuleB->getHalfHeight(),capsuleB->getRadius(),capsuleA->getUpAxis(),capsuleB->getUpAxis(),
			body0->getWorldTransform(),body1->getWorldTransform(),threshold);

		if (dist<threshold)
		{
			btAssert(normalOnB.length2()>=(SIMD_EPSILON*SIMD_EPSILON));
			resultOut->addContactPoint(normalOnB,pointOnBWorld,dist);	
		}
		resultOut->refreshContactPoints();
		return;
	}
#endif //BT_DISABLE_CAPSULE_CAPSULE_COLLIDER


#ifdef USE_SEPDISTANCE_UTIL2
	m_sepDistance.updateSeparatingDistance(body0->getWorldTransform(),body1->getWorldTransform());
	if (!dispatchInfo.m_useConvexConservativeDistanceUtil || m_sepDistance.getConservativeSeparatingDistance()<=0.f)
#endif //USE_SEPDISTANCE_UTIL2

	{

	
	btGjkPairDetector::ClosestPointInput input;

	btGjkPairDetector	gjkPairDetector(min0,min1,m_simplexSolver,m_pdSolver);
	//TODO: if (dispatchInfo.m_useContinuous)
	gjkPairDetector.setMinkowskiA(min0);
	gjkPairDetector.setMinkowskiB(min1);

#ifdef USE_SEPDISTANCE_UTIL2
	if (dispatchInfo.m_useConvexConservativeDistanceUtil)
	{
		input.m_maximumDistanceSquared = BT_LARGE_FLOAT;
	} else
#endif //USE_SEPDISTANCE_UTIL2
	{
		// VC9 (MMD 0x4E18B0): the threshold sum is folded in double precision
		// as (marginB + (threshold + marginA)), squared in double, and only the
		// final product is rounded to float.
		const double mmdThresholdSum =
			(double)min1->getMargin()
			+ ((double)m_manifoldPtr->getContactBreakingThreshold()
				+ (double)min0->getMargin());
		input.m_maximumDistanceSquared =
			(btScalar)(mmdThresholdSum * mmdThresholdSum);
	}

	input.m_stackAlloc = dispatchInfo.m_stackAllocator;
	input.m_transformA = body0->getWorldTransform();
	input.m_transformB = body1->getWorldTransform();

	gjkPairDetector.getClosestPoints(input,*resultOut,dispatchInfo.m_debugDraw);

	btVector3 v0,v1;
	btVector3 sepNormalWorldSpace;
	

#ifdef USE_SEPDISTANCE_UTIL2
	btScalar sepDist = 0.f;
	if (dispatchInfo.m_useConvexConservativeDistanceUtil)
	{
		sepDist = gjkPairDetector.getCachedSeparatingDistance();
		if (sepDist>SIMD_EPSILON)
		{
			sepDist += dispatchInfo.m_convexConservativeDistanceThreshold;
			//now perturbe directions to get multiple contact points
			sepNormalWorldSpace = gjkPairDetector.getCachedSeparatingAxis().normalized();
			btPlaneSpace1(sepNormalWorldSpace,v0,v1);
		}
	}
#endif //USE_SEPDISTANCE_UTIL2

	//now perform 'm_numPerturbationIterations' collision queries with the perturbated collision objects
	
	//perform perturbation when more then 'm_minimumPointsPerturbationThreshold' points
	if (resultOut->getPersistentManifold()->getNumContacts() < m_minimumPointsPerturbationThreshold)
	{
		
		int i;

		bool perturbeA = true;
		const btScalar angleLimit = 0.125f * SIMD_PI;
		btScalar perturbeAngle;
		btScalar radiusA = min0->getAngularMotionDisc();
		btScalar radiusB = min1->getAngularMotionDisc();
		if (radiusA < radiusB)
		{
			perturbeAngle = gContactBreakingThreshold /radiusA;
			perturbeA = true;
		} else
		{
			perturbeAngle = gContactBreakingThreshold / radiusB;
			perturbeA = false;
		}
		if ( perturbeAngle > angleLimit ) 
				perturbeAngle = angleLimit;

		btTransform unPerturbedTransform;
		if (perturbeA)
		{
			unPerturbedTransform = input.m_transformA;
		} else
		{
			unPerturbedTransform = input.m_transformB;
		}
		
		for ( i=0;i<m_numPerturbationIterations;i++)
		{
			btQuaternion perturbeRot(v0,perturbeAngle);
			btScalar iterationAngle = i*(SIMD_2_PI/btScalar(m_numPerturbationIterations));
			btQuaternion rotq(sepNormalWorldSpace,iterationAngle);
			
			
			if (perturbeA)
			{
				input.m_transformA.setBasis(  btMatrix3x3(rotq.inverse()*perturbeRot*rotq)*body0->getWorldTransform().getBasis());
				input.m_transformB = body1->getWorldTransform();
#ifdef DEBUG_CONTACTS
				dispatchInfo.m_debugDraw->drawTransform(input.m_transformA,10.0);
#endif //DEBUG_CONTACTS
			} else
			{
				input.m_transformA = body0->getWorldTransform();
				input.m_transformB.setBasis( btMatrix3x3(rotq.inverse()*perturbeRot*rotq)*body1->getWorldTransform().getBasis());
#ifdef DEBUG_CONTACTS
				dispatchInfo.m_debugDraw->drawTransform(input.m_transformB,10.0);
#endif
			}
			
			btPerturbedContactResult perturbedResultOut(resultOut,input.m_transformA,input.m_transformB,unPerturbedTransform,perturbeA,dispatchInfo.m_debugDraw);
			gjkPairDetector.getClosestPoints(input,perturbedResultOut,dispatchInfo.m_debugDraw);
			
			
		}
	}

	

#ifdef USE_SEPDISTANCE_UTIL2
	if (dispatchInfo.m_useConvexConservativeDistanceUtil && (sepDist>SIMD_EPSILON))
	{
		m_sepDistance.initSeparatingDistance(gjkPairDetector.getCachedSeparatingAxis(),sepDist,body0->getWorldTransform(),body1->getWorldTransform());
	}
#endif //USE_SEPDISTANCE_UTIL2


	}

	if (m_ownManifold)
	{
		resultOut->refreshContactPoints();
	}

}



bool disableCcd = false;
btScalar	btConvexConvexAlgorithm::calculateTimeOfImpact(btCollisionObject* col0,btCollisionObject* col1,const btDispatcherInfo& dispatchInfo,btManifoldResult* resultOut)
{
	(void)resultOut;
	(void)dispatchInfo;
	///Rather then checking ALL pairs, only calculate TOI when motion exceeds threshold
    
	///Linear motion for one of objects needs to exceed m_ccdSquareMotionThreshold
	///col0->m_worldTransform,
	btScalar resultFraction = btScalar(1.);


	btScalar squareMot0 = (col0->getInterpolationWorldTransform().getOrigin() - col0->getWorldTransform().getOrigin()).length2();
	btScalar squareMot1 = (col1->getInterpolationWorldTransform().getOrigin() - col1->getWorldTransform().getOrigin()).length2();
    
	if (squareMot0 < col0->getCcdSquareMotionThreshold() &&
		squareMot1 < col1->getCcdSquareMotionThreshold())
		return resultFraction;

	if (disableCcd)
		return btScalar(1.);


	//An adhoc way of testing the Continuous Collision Detection algorithms
	//One object is approximated as a sphere, to simplify things
	//Starting in penetration should report no time of impact
	//For proper CCD, better accuracy and handling of 'allowed' penetration should be added
	//also the mainloop of the physics should have a kind of toi queue (something like Brian Mirtich's application of Timewarp for Rigidbodies)

		
	/// Convex0 against sphere for Convex1
	{
		btConvexShape* convex0 = static_cast<btConvexShape*>(col0->getCollisionShape());

		btSphereShape	sphere1(col1->getCcdSweptSphereRadius()); //todo: allow non-zero sphere sizes, for better approximation
		btConvexCast::CastResult result;
		btVoronoiSimplexSolver voronoiSimplex;
		//SubsimplexConvexCast ccd0(&sphere,min0,&voronoiSimplex);
		///Simplification, one object is simplified as a sphere
		btGjkConvexCast ccd1( convex0 ,&sphere1,&voronoiSimplex);
		//ContinuousConvexCollision ccd(min0,min1,&voronoiSimplex,0);
		if (ccd1.calcTimeOfImpact(col0->getWorldTransform(),col0->getInterpolationWorldTransform(),
			col1->getWorldTransform(),col1->getInterpolationWorldTransform(),result))
		{
		
			//store result.m_fraction in both bodies
		
			if (col0->getHitFraction()> result.m_fraction)
				col0->setHitFraction( result.m_fraction );

			if (col1->getHitFraction() > result.m_fraction)
				col1->setHitFraction( result.m_fraction);

			if (resultFraction > result.m_fraction)
				resultFraction = result.m_fraction;

		}
		
		


	}

	/// Sphere (for convex0) against Convex1
	{
		btConvexShape* convex1 = static_cast<btConvexShape*>(col1->getCollisionShape());

		btSphereShape	sphere0(col0->getCcdSweptSphereRadius()); //todo: allow non-zero sphere sizes, for better approximation
		btConvexCast::CastResult result;
		btVoronoiSimplexSolver voronoiSimplex;
		//SubsimplexConvexCast ccd0(&sphere,min0,&voronoiSimplex);
		///Simplification, one object is simplified as a sphere
		btGjkConvexCast ccd1(&sphere0,convex1,&voronoiSimplex);
		//ContinuousConvexCollision ccd(min0,min1,&voronoiSimplex,0);
		if (ccd1.calcTimeOfImpact(col0->getWorldTransform(),col0->getInterpolationWorldTransform(),
			col1->getWorldTransform(),col1->getInterpolationWorldTransform(),result))
		{
		
			//store result.m_fraction in both bodies
		
			if (col0->getHitFraction()	> result.m_fraction)
				col0->setHitFraction( result.m_fraction);

			if (col1->getHitFraction() > result.m_fraction)
				col1->setHitFraction( result.m_fraction);

			if (resultFraction > result.m_fraction)
				resultFraction = result.m_fraction;

		}
	}
	
	return resultFraction;

}

