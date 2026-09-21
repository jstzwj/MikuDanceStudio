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

#include "btGjkPairDetector.h"
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "BulletCollision/NarrowPhaseCollision/btSimplexSolverInterface.h"
#include "BulletCollision/NarrowPhaseCollision/btConvexPenetrationDepthSolver.h"



#if defined(DEBUG) || defined (_DEBUG)
//#define TEST_NON_VIRTUAL 1
#include <stdio.h> //for debug printf
#ifdef __SPU__
#include <spu_printf.h>
#define printf spu_printf
//#define DEBUG_SPU_COLLISION_DETECTION 1
#endif //__SPU__
#endif

//must be above the machine epsilon
#define REL_ERROR2 btScalar(1.0e-6)

//temp globals, to improve GJK/EPA/penetration calculations
int gNumDeepPenetrationChecks = 0;
int gNumGjkChecks = 0;

// VC9 keeps 1/sqrt(length2) in an x87 register while it scales all three
// components in the penetration-result path.  Modern MSVC rounds the square
// root to float first and emits divss/mulss, which changes MMD contact normals.
static __declspec(noinline) void mmdGjkNormalizeX87(btVector3& value,
													btScalar lengthSquared)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	btScalar* components = &value[0];
	__asm
	{
		mov eax, components
		fld dword ptr [lengthSquared]
		fsqrt
		fld1
		fdivrp st(1), st

		fld dword ptr [eax]
		fmul st, st(1)
		fstp dword ptr [eax]
		fld dword ptr [eax+4]
		fmul st, st(1)
		fstp dword ptr [eax+4]
		fld dword ptr [eax+8]
		fmul st, st(1)
		fstp dword ptr [eax+8]
		fstp st(0)
	}
#else
	value /= btSqrt(lengthSquared);
#endif
}

// VC9 tail (MMD 0x4E4673): rlen = 1/sqrt(lenSqr) is kept extended on the
// x87 stack while every normalInB component is scaled once; the float copy
// of rlen feeds the (1/rlen - margin) distance, and s is the extended sqrt
// of squaredDistance rounded once to float.
static __declspec(noinline) void mmdGjkTailNormalize(btVector3& normal,
                                                    btScalar lenSqr,
                                                    btScalar squaredDistance,
                                                    btScalar* rlenOut,
                                                    btScalar* sOut)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	btScalar* components = &normal.m_floats[0];
	__asm
	{
		mov eax, components
		mov edx, rlenOut
		fld dword ptr [lenSqr]
		fsqrt
		fld1
		fdivrp st(1), st		; st0 = 1/sqrt(lenSqr), extended
		fst dword ptr [edx]		; float copy for the distance term
		fld dword ptr [eax]
		fmul st, st(1)
		fstp dword ptr [eax]
		fld dword ptr [eax+4]
		fmul st, st(1)
		fstp dword ptr [eax+4]
		fld dword ptr [eax+8]
		fmul st, st(1)
		fstp dword ptr [eax+8]
		fstp st(0)
		mov edx, sOut
		fld dword ptr [squaredDistance]
		fsqrt
		fstp dword ptr [edx]
	}
#else
	const btScalar rlen = btScalar(1.)/btSqrt(lenSqr);
	normal *= rlen;
	*rlenOut = rlen;
	*sOut = btSqrt(squaredDistance);
#endif
}

// MMD 0x4E4970: distance2 = -(float)sqrt(((dz*dz + dy*dy) + dx*dx)) with the
// differences, squares, sum and square root all kept extended on the x87
// stack; a single rounding happens at the final store.
static __declspec(noinline) btScalar mmdGjkNegLengthZXY(const btVector3& a,
												   const btVector3& b)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	const float* pa = a.m_floats;
	const float* pb = b.m_floats;
	btScalar result;
	__asm
	{
		mov eax, pa
		mov edx, pb
		fld dword ptr [eax]
		fsub dword ptr [edx]		; dx
		fld dword ptr [eax+4]
		fsub dword ptr [edx+4]	; dy
		fld dword ptr [eax+8]
		fsub dword ptr [edx+8]	; dz
		fmul st, st				; dz*dz
		fld st(1)
		fmulp st(2), st			; dy*dy
		faddp st(1), st			; dz*dz + dy*dy
		fld st(1)
		fmulp st(2), st			; dx*dx
		faddp st(1), st			; (dz*dz + dy*dy) + dx*dx
		fsqrt
		fchs
		fstp dword ptr [result]
	}
	return result;
#else
	const btVector3 d = a - b;
	return -btSqrt((d.z()*d.z() + d.y()*d.y()) + d.x()*d.x());
#endif
}

// MMD 0x4E4B30: distance2 = (float)(sqrt(((dz*dz + dy*dy) + dx*dx)) - margin),
// everything extended until the single float store.
static __declspec(noinline) btScalar mmdGjkLengthMinusMarginZXY(
	const btVector3& a, const btVector3& b, btScalar margin)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	const float* pa = a.m_floats;
	const float* pb = b.m_floats;
	btScalar result;
	__asm
	{
		mov eax, pa
		mov edx, pb
		fld dword ptr [eax]
		fsub dword ptr [edx]		; dx
		fld dword ptr [eax+4]
		fsub dword ptr [edx+4]	; dy
		fld dword ptr [eax+8]
		fsub dword ptr [edx+8]	; dz
		fmul st, st				; dz*dz
		fld st(1)
		fmulp st(2), st			; dy*dy
		faddp st(1), st			; dz*dz + dy*dy
		fld st(1)
		fmulp st(2), st			; dx*dx
		faddp st(1), st			; (dz*dz + dy*dy) + dx*dx
		fsqrt
		fsub dword ptr [margin]
		fstp dword ptr [result]
	}
	return result;
#else
	const btVector3 d = a - b;
	return btSqrt((d.z()*d.z() + d.y()*d.y()) + d.x()*d.x()) - margin;
#endif
}


btGjkPairDetector::btGjkPairDetector(const btConvexShape* objectA,const btConvexShape* objectB,btSimplexSolverInterface* simplexSolver,btConvexPenetrationDepthSolver*	penetrationDepthSolver)
:m_cachedSeparatingAxis(btScalar(0.),btScalar(1.),btScalar(0.)),
m_penetrationDepthSolver(penetrationDepthSolver),
m_simplexSolver(simplexSolver),
m_minkowskiA(objectA),
m_minkowskiB(objectB),
m_shapeTypeA(objectA->getShapeType()),
m_shapeTypeB(objectB->getShapeType()),
m_marginA(objectA->getMargin()),
m_marginB(objectB->getMargin()),
m_ignoreMargin(false),
m_lastUsedMethod(-1),
m_catchDegeneracies(1)
{
}
btGjkPairDetector::btGjkPairDetector(const btConvexShape* objectA,const btConvexShape* objectB,int shapeTypeA,int shapeTypeB,btScalar marginA, btScalar marginB, btSimplexSolverInterface* simplexSolver,btConvexPenetrationDepthSolver*	penetrationDepthSolver)
:m_cachedSeparatingAxis(btScalar(0.),btScalar(1.),btScalar(0.)),
m_penetrationDepthSolver(penetrationDepthSolver),
m_simplexSolver(simplexSolver),
m_minkowskiA(objectA),
m_minkowskiB(objectB),
m_shapeTypeA(shapeTypeA),
m_shapeTypeB(shapeTypeB),
m_marginA(marginA),
m_marginB(marginB),
m_ignoreMargin(false),
m_lastUsedMethod(-1),
m_catchDegeneracies(1)
{
}

void	btGjkPairDetector::getClosestPoints(const ClosestPointInput& input,Result& output,class btIDebugDraw* debugDraw,bool swapResults)
{
	(void)swapResults;

	getClosestPointsNonVirtual(input,output,debugDraw);
}

#ifdef __SPU__
void btGjkPairDetector::getClosestPointsNonVirtual(const ClosestPointInput& input,Result& output,class btIDebugDraw* debugDraw)
#else
void btGjkPairDetector::getClosestPointsNonVirtual(const ClosestPointInput& input,Result& output,class btIDebugDraw* debugDraw)
#endif
{
	m_cachedSeparatingDistance = 0.f;

	btScalar distance=btScalar(0.);
	btVector3	normalInB(btScalar(0.),btScalar(0.),btScalar(0.));
	btVector3 pointOnA,pointOnB;
	btTransform	localTransA = input.m_transformA;
	btTransform localTransB = input.m_transformB;
	btVector3 positionOffset = (localTransA.getOrigin() + localTransB.getOrigin()) * btScalar(0.5);
	localTransA.getOrigin() -= positionOffset;
	localTransB.getOrigin() -= positionOffset;

	bool check2d = m_minkowskiA->isConvex2d() && m_minkowskiB->isConvex2d();

	btScalar marginA = m_marginA;
	btScalar marginB = m_marginB;

	gNumGjkChecks++;

#ifdef DEBUG_SPU_COLLISION_DETECTION
	spu_printf("inside gjk\n");
#endif
	//for CCD we don't use margins
	if (m_ignoreMargin)
	{
		marginA = btScalar(0.);
		marginB = btScalar(0.);
#ifdef DEBUG_SPU_COLLISION_DETECTION
		spu_printf("ignoring margin\n");
#endif
	}

	m_curIter = 0;
	int gGjkMaxIter = 1000;//this is to catch invalid input, perhaps check for #NaN?
	m_cachedSeparatingAxis.setValue(0,1,0);

	bool isValid = false;
	bool checkSimplex = false;
	bool checkPenetration = true;
	m_degenerateSimplex = 0;

	m_lastUsedMethod = -1;

	{
		btScalar squaredDistance = BT_LARGE_FLOAT;
		btScalar delta = btScalar(0.);
		
		btScalar margin = marginA + marginB;
		
		

		m_simplexSolver->reset();
		
			for ( ; ; )
			{
				// VC9 orders (MMD 0x4E40F0): the separating axes and the world
				// transforms use per-component groupings that differ from the
				// plain btVector3 dot; replicate them exactly.
				const btScalar axisX = m_cachedSeparatingAxis.x();
				const btScalar axisY = m_cachedSeparatingAxis.y();
				const btScalar axisZ = m_cachedSeparatingAxis.z();
				const btScalar negX = -axisX;
				const btScalar negY = -axisY;
				const btScalar negZ = -axisZ;
				const btMatrix3x3& basisA = input.m_transformA.getBasis();
				const btMatrix3x3& basisB = input.m_transformB.getBasis();
				btVector3 seperatingAxisInA(
					(basisA[0].x()*negX + basisA[2].x()*negZ) + basisA[1].x()*negY,
					(basisA[0].y()*negX + basisA[1].y()*negY) + basisA[2].y()*negZ,
					(basisA[2].z()*negZ + basisA[0].z()*negX) + basisA[1].z()*negY);
				btVector3 seperatingAxisInB(
					(basisB[2].x()*axisZ + basisB[0].x()*axisX) + basisB[1].x()*axisY,
					(basisB[1].y()*axisY + basisB[2].y()*axisZ) + basisB[0].y()*axisX,
					(basisB[1].z()*axisY + basisB[2].z()*axisZ) + basisB[0].z()*axisX);

				btVector3 pInA = m_minkowskiA->localGetSupportVertexWithoutMarginNonVirtual(seperatingAxisInA);
				btVector3 qInB = m_minkowskiB->localGetSupportVertexWithoutMarginNonVirtual(seperatingAxisInB);

				btVector3  pWorld = btVector3(
					((basisA[0].y()*pInA.y() + basisA[0].z()*pInA.z()) + basisA[0].x()*pInA.x()) + localTransA.getOrigin().x(),
					((basisA[1].y()*pInA.y() + basisA[1].z()*pInA.z()) + basisA[1].x()*pInA.x()) + localTransA.getOrigin().y(),
					((basisA[2].y()*pInA.y() + basisA[2].z()*pInA.z()) + basisA[2].x()*pInA.x()) + localTransA.getOrigin().z());
				btVector3  qWorld = btVector3(
					((basisB[0].y()*qInB.y() + basisB[0].z()*qInB.z()) + basisB[0].x()*qInB.x()) + localTransB.getOrigin().x(),
					((basisB[1].y()*qInB.y() + basisB[1].z()*qInB.z()) + basisB[1].x()*qInB.x()) + localTransB.getOrigin().y(),
					((basisB[2].y()*qInB.y() + basisB[2].z()*qInB.z()) + basisB[2].x()*qInB.x()) + localTransB.getOrigin().z());

				btVector3 w	= pWorld - qWorld;
				const btScalar delta = (axisZ*w.z() + axisY*w.y()) + axisX*w.x();

				// potential exit, they don't overlap
				if ((delta > btScalar(0.0)) && (delta * delta > squaredDistance * input.m_maximumDistanceSquared))
				{
					// MMD's build leaves m_degenerateSimplex untouched here.
					checkSimplex=true;
					break;
				}

				//exit 0: the new point is already in the simplex, or we didn't come any closer
				if (m_simplexSolver->inSimplex(w))
				{
					m_degenerateSimplex = 1;
					checkSimplex = true;
					break;
				}
				// are we getting any closer ?
				btScalar f0 = squaredDistance - delta;
				btScalar f1 = squaredDistance * REL_ERROR2;

				if (f0 <= f1)
				{
					if (f0 <= btScalar(0.))
					{
						m_degenerateSimplex = 2;
					} else
					{
						m_degenerateSimplex = 11;
					}
					checkSimplex = true;
					break;
				}

				//add current vertex to simplex
				m_simplexSolver->addVertex(w, pWorld, qWorld);

				btVector3 newCachedSeparatingAxis;

				//calculate the closest point to the origin (update vector v)
				if (!m_simplexSolver->closest(newCachedSeparatingAxis))
				{
					m_degenerateSimplex = 3;
					checkSimplex = true;
					break;
				}

				const btScalar newSqDist = newCachedSeparatingAxis.length2();
				if(newSqDist < REL_ERROR2)
				{
					m_cachedSeparatingAxis = newCachedSeparatingAxis;
					m_degenerateSimplex = 6;
					checkSimplex = true;
					break;
				}

				btScalar previousSquaredDistance = squaredDistance;
				squaredDistance = newSqDist;

				//this termination condition is ACTIVE in MMD's build (unlike the
				//stock 2.75 #if 0 block): bail without restoring the distance and
				//without updating the separating axis.
				if (squaredDistance > previousSquaredDistance)
				{
					m_degenerateSimplex = 7;
					break;
				}

				m_cachedSeparatingAxis = newCachedSeparatingAxis;

				//are we getting any closer ?
				if (previousSquaredDistance - squaredDistance <= SIMD_EPSILON * previousSquaredDistance)
				{
					m_simplexSolver->backup_closest(m_cachedSeparatingAxis);
					checkSimplex = true;
					m_degenerateSimplex = 12;
					break;
				}

				  //degeneracy, this is typically due to invalid/uninitialized worldtransforms for a btCollisionObject
				  if (m_curIter++ > gGjkMaxIter)
				  {
					  break;
				  }

				if (m_simplexSolver->fullSimplex())
				{
					// MMD's build does not set m_degenerateSimplex here.
					m_simplexSolver->backup_closest(m_cachedSeparatingAxis);
					break;
				}
			}

			if (checkSimplex)
			{
				m_simplexSolver->compute_points(pointOnA, pointOnB);
				normalInB = pointOnA-pointOnB;
				btScalar lenSqr =m_cachedSeparatingAxis.length2();

				//valid normal; MMD compares against the double 1e-4 constant.
				if ((double)lenSqr < 0.0001)
				{
					m_degenerateSimplex = 5;
				}
				if (lenSqr > SIMD_EPSILON*SIMD_EPSILON)
				{
					btScalar rlen, s;
					mmdGjkTailNormalize(normalInB, lenSqr, squaredDistance, &rlen, &s);
					const btScalar invS = btScalar(1.) / s;

					pointOnA -= m_cachedSeparatingAxis * (invS * marginA);
					pointOnB += m_cachedSeparatingAxis * (invS * marginB);
					distance = ((btScalar(1.)/rlen) - margin);
					isValid = true;

					m_lastUsedMethod = 1;
				} else
				{
					m_lastUsedMethod = 2;
				}
			}

		bool catchDegeneratePenetrationCase = 
			(m_catchDegeneracies && m_penetrationDepthSolver && m_degenerateSimplex && ((distance+margin) < 0.01));

		//if (checkPenetration && !isValid)
		if (checkPenetration && (!isValid || catchDegeneratePenetrationCase ))
		{
			//penetration case

			//if there is no way to handle penetrations, bail out
			if (m_penetrationDepthSolver)
			{
				// Penetration depth case.
				btVector3 tmpPointOnA,tmpPointOnB;
				
				gNumDeepPenetrationChecks++;
				m_cachedSeparatingAxis.setZero();

				bool isValid2 = m_penetrationDepthSolver->calcPenDepth( 
					*m_simplexSolver, 
					m_minkowskiA,m_minkowskiB,
					localTransA,localTransB,
					m_cachedSeparatingAxis, tmpPointOnA, tmpPointOnB,
					debugDraw,input.m_stackAlloc
					);


				if (isValid2)
				{
					btVector3 tmpNormalInB = tmpPointOnB-tmpPointOnA;
					// MMD 0x4E4AF3: ((z*z + y*y) + x*x).
					btScalar lenSqr = (tmpNormalInB.z()*tmpNormalInB.z()
						+ tmpNormalInB.y()*tmpNormalInB.y())
						+ tmpNormalInB.x()*tmpNormalInB.x();
					if (lenSqr <= (SIMD_EPSILON*SIMD_EPSILON))
					{
						tmpNormalInB = m_cachedSeparatingAxis;
						lenSqr = m_cachedSeparatingAxis.length2();
					}

					if (lenSqr > (SIMD_EPSILON*SIMD_EPSILON))
					{
						mmdGjkNormalizeX87(tmpNormalInB,lenSqr);
						btScalar distance2 = mmdGjkNegLengthZXY(tmpPointOnA,tmpPointOnB);
						//only replace valid penetrations when the result is deeper (check)
						if (!isValid || (distance2 < distance))
						{
							distance = distance2;
							pointOnA = tmpPointOnA;
							pointOnB = tmpPointOnB;
							normalInB = tmpNormalInB;
							isValid = true;
							m_lastUsedMethod = 3;
						} else
						{
							m_lastUsedMethod = 8;
						}
					} else
					{
						m_lastUsedMethod = 9;
					}
				} else

				{
					///this is another degenerate case, where the initial GJK calculation reports a degenerate case
					///EPA reports no penetration, and the second GJK (using the supporting vector without margin)
					///reports a valid positive distance. Use the results of the second GJK instead of failing.
					///thanks to Jacob.Langford for the reproduction case
					///http://code.google.com/p/bullet/issues/detail?id=250

				
					if (m_cachedSeparatingAxis.length2() > btScalar(0.))
					{
						btScalar distance2 = mmdGjkLengthMinusMarginZXY(tmpPointOnA,tmpPointOnB,margin);
						//only replace valid distances when the distance is less
						if (!isValid || (distance2 < distance))
						{
							distance = distance2;
							pointOnA = tmpPointOnA;
							pointOnB = tmpPointOnB;
							pointOnA -= m_cachedSeparatingAxis * marginA ;
							pointOnB += m_cachedSeparatingAxis * marginB ;
							normalInB = m_cachedSeparatingAxis;	// MMD does not renormalize here
							isValid = true;
							m_lastUsedMethod = 6;
						} else
						{
							m_lastUsedMethod = 5;
						}
					}
				}
				
			}

		}
	}

	

	// MMD 0x4E3E10 (LABEL_45): the stock 2.75 distance gate is GONE in the
	// shipped binary -- the contact is emitted whenever the GJK run is valid,
	// regardless of distance vs. m_maximumDistanceSquared.
	if (isValid)
	{
#if 0
///some debugging
//		if (check2d)
		{
			printf("n = %2.3f,%2.3f,%2.3f. ",normalInB[0],normalInB[1],normalInB[2]);
			printf("distance = %2.3f exit=%d deg=%d\n",distance,m_lastUsedMethod,m_degenerateSimplex);
		}
#endif 

		m_cachedSeparatingAxis = normalInB;
		m_cachedSeparatingDistance = distance;

		output.addContactPoint(
			normalInB,
			pointOnB+positionOffset,
			distance);

	}


}





