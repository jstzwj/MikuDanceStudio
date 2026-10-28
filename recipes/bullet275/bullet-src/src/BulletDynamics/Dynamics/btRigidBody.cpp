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

#include "btRigidBody.h"
#include "LinearMath/btMmdMath.h"
#include <cstdio>
#include <cstdlib>
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "LinearMath/btMinMax.h"
#include "LinearMath/btTransformUtil.h"
#include "LinearMath/btMotionState.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#if defined(_MSC_VER) && defined(_M_IX86)
#include <xmmintrin.h>
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#if defined(BT_USE_DOUBLE_PRECISION)
static SIMD_FORCE_INLINE btScalar mmdInertiaMul(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdInertiaAdd(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdInertiaElement120(
	const btMatrix3x3& basis, const btVector3& local, int row, int column)
{
	const btScalar p0 = mmdInertiaMul(
		mmdInertiaMul(basis[row][0], local[0]), basis[column][0]);
	const btScalar p1 = mmdInertiaMul(
		mmdInertiaMul(basis[row][1], local[1]), basis[column][1]);
	const btScalar p2 = mmdInertiaMul(
		mmdInertiaMul(basis[row][2], local[2]), basis[column][2]);
	return mmdInertiaAdd(mmdInertiaAdd(p1, p2), p0);
}

static SIMD_FORCE_INLINE btScalar mmdInertiaElement021(
	const btMatrix3x3& basis, const btVector3& local, int row, int column)
{
	const btScalar p0 = mmdInertiaMul(
		mmdInertiaMul(basis[row][0], local[0]), basis[column][0]);
	const btScalar p1 = mmdInertiaMul(
		mmdInertiaMul(basis[row][1], local[1]), basis[column][1]);
	const btScalar p2 = mmdInertiaMul(
		mmdInertiaMul(basis[row][2], local[2]), basis[column][2]);
	return mmdInertiaAdd(mmdInertiaAdd(p0, p2), p1);
}

static SIMD_FORCE_INLINE btScalar mmdInertiaElement201(
	const btMatrix3x3& basis, const btVector3& local, int row, int column)
{
	const btScalar p0 = mmdInertiaMul(
		mmdInertiaMul(basis[row][0], local[0]), basis[column][0]);
	const btScalar p1 = mmdInertiaMul(
		mmdInertiaMul(basis[row][1], local[1]), basis[column][1]);
	const btScalar p2 = mmdInertiaMul(
		mmdInertiaMul(basis[row][2], local[2]), basis[column][2]);
	return mmdInertiaAdd(mmdInertiaAdd(p2, p0), p1);
}

static SIMD_FORCE_INLINE btScalar mmdInertiaElement210(
	const btMatrix3x3& basis, const btVector3& local, int row, int column)
{
	const btScalar p0 = mmdInertiaMul(
		mmdInertiaMul(basis[row][0], local[0]), basis[column][0]);
	const btScalar p1 = mmdInertiaMul(
		mmdInertiaMul(basis[row][1], local[1]), basis[column][1]);
	const btScalar p2 = mmdInertiaMul(
		mmdInertiaMul(basis[row][2], local[2]), basis[column][2]);
	return mmdInertiaAdd(mmdInertiaAdd(p2, p1), p0);
}

static btMatrix3x3 mmdInertiaTensorWorldVc9(
	const btMatrix3x3& basis, const btVector3& local)
{
	/* VC9's inlined scaled(basis) * transpose(basis) reduces each output
	   element with its own term order (MMD 0x4F08E4..0x4F094C):
	     [0][0]=(t2+t0)+t1  [0][1]=(t2+t1)+t0  [0][2]=(t2+t0)+t1
	     [1][0]=(t1+t2)+t0  [1][1]=(t1+t2)+t0  [1][2]=(t2+t1)+t0
	     [2][0]=(t1+t2)+t0  [2][1]=(t1+t2)+t0  [2][2]=(t2+t1)+t0
	   The asymmetry is observable through body8's invInertia[0][1]. */
	return btMatrix3x3(
		mmdInertiaElement201(basis, local, 0, 0),
		mmdInertiaElement210(basis, local, 0, 1),
		mmdInertiaElement201(basis, local, 0, 2),
		mmdInertiaElement120(basis, local, 1, 0),
		mmdInertiaElement120(basis, local, 1, 1),
		mmdInertiaElement210(basis, local, 1, 2),
		mmdInertiaElement120(basis, local, 2, 0),
		mmdInertiaElement120(basis, local, 2, 1),
		mmdInertiaElement210(basis, local, 2, 2));
}

#endif

static SIMD_FORCE_INLINE btScalar mmdVelocitySub(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityMul(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityAdd(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityDiv(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityDot021(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	btScalar sum = mmdVelocityAdd(
		mmdVelocityMul(a0, b0), mmdVelocityMul(a2, b2));
	return mmdVelocityAdd(sum, mmdVelocityMul(a1, b1));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityDot012(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	btScalar sum = mmdVelocityAdd(
		mmdVelocityMul(a0, b0), mmdVelocityMul(a1, b1));
	return mmdVelocityAdd(sum, mmdVelocityMul(a2, b2));
}

static SIMD_FORCE_INLINE btScalar mmdVelocityDot120(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	btScalar sum = mmdVelocityAdd(
		mmdVelocityMul(a1, b1), mmdVelocityMul(a2, b2));
	return mmdVelocityAdd(sum, mmdVelocityMul(a0, b0));
}

static btMatrix3x3 mmdVelocityInverseMatrixVc9(const btMatrix3x3& a)
{
	const btScalar co0 = mmdVelocitySub(
		mmdVelocityMul(a[1][1], a[2][2]), mmdVelocityMul(a[1][2], a[2][1]));
	const btScalar co1 = mmdVelocitySub(
		mmdVelocityMul(a[1][2], a[2][0]), mmdVelocityMul(a[1][0], a[2][2]));
	const btScalar co2 = mmdVelocitySub(
		mmdVelocityMul(a[1][0], a[2][1]), mmdVelocityMul(a[1][1], a[2][0]));
	// MMD 0x4F002C..0x4F0040 is (term0 + term1) + term2.
	const btScalar determinant = mmdVelocityDot012(
		a[0][0], co0, a[0][1], co1, a[0][2], co2);
	const btScalar s = mmdVelocityDiv(btScalar(1.0), determinant);
	return btMatrix3x3(
		mmdVelocityMul(co0, s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][2], a[2][1]), mmdVelocityMul(a[0][1], a[2][2])), s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][1], a[1][2]), mmdVelocityMul(a[0][2], a[1][1])), s),
		mmdVelocityMul(co1, s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][0], a[2][2]), mmdVelocityMul(a[0][2], a[2][0])), s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][2], a[1][0]), mmdVelocityMul(a[0][0], a[1][2])), s),
		mmdVelocityMul(co2, s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][1], a[2][0]), mmdVelocityMul(a[0][0], a[2][1])), s),
		mmdVelocityMul(mmdVelocitySub(
			mmdVelocityMul(a[0][0], a[1][1]), mmdVelocityMul(a[0][1], a[1][0])), s));
}

static btMatrix3x3 mmdVelocityRelativeBasisVc9(
	const btMatrix3x3& oldBasis, const btMatrix3x3& newBasis)
{
	const btMatrix3x3 inverse = mmdVelocityInverseMatrixVc9(oldBasis);
	return btMatrix3x3(
		mmdVelocityDot021(newBasis[0][0], inverse[0][0], newBasis[0][1], inverse[1][0], newBasis[0][2], inverse[2][0]),
		mmdVelocityDot021(newBasis[0][0], inverse[0][1], newBasis[0][1], inverse[1][1], newBasis[0][2], inverse[2][1]),
		mmdVelocityDot021(newBasis[0][0], inverse[0][2], newBasis[0][1], inverse[1][2], newBasis[0][2], inverse[2][2]),
		mmdVelocityDot120(newBasis[1][0], inverse[0][0], newBasis[1][1], inverse[1][0], newBasis[1][2], inverse[2][0]),
		mmdVelocityDot120(newBasis[1][0], inverse[0][1], newBasis[1][1], inverse[1][1], newBasis[1][2], inverse[2][1]),
		mmdVelocityDot120(newBasis[1][0], inverse[0][2], newBasis[1][1], inverse[1][2], newBasis[1][2], inverse[2][2]),
		mmdVelocityDot120(newBasis[2][0], inverse[0][0], newBasis[2][1], inverse[1][0], newBasis[2][2], inverse[2][0]),
		mmdVelocityDot120(newBasis[2][0], inverse[0][1], newBasis[2][1], inverse[1][1], newBasis[2][2], inverse[2][1]),
		mmdVelocityDot120(newBasis[2][0], inverse[0][2], newBasis[2][1], inverse[1][2], newBasis[2][2], inverse[2][2]));
}

static void mmdVelocityQuaternionVc9(
	const btMatrix3x3& matrix, btScalar quaternion[4])
{
	const btScalar zero = btScalar(0.0);
	const btScalar half = btScalar(0.5);
	btScalar trace = mmdVelocityAdd(
		mmdVelocityAdd(matrix[0][0], matrix[1][1]), matrix[2][2]);

	if (trace > zero)
	{
		btScalar root;
		btScalar halfRoot;
		btMmdMath::quaternionRootParts(trace, 0.0f, 0.0f, true, &root, &halfRoot);
		const btScalar scale = mmdVelocityDiv(half, root);
		quaternion[0] = mmdVelocityMul(
			mmdVelocitySub(matrix[2][1], matrix[1][2]), scale);
		quaternion[1] = mmdVelocityMul(
			mmdVelocitySub(matrix[0][2], matrix[2][0]), scale);
		quaternion[2] = mmdVelocityMul(
			mmdVelocitySub(matrix[1][0], matrix[0][1]), scale);
		quaternion[3] = halfRoot;
		return;
	}

	int i;
	if (matrix[1][1] > matrix[0][0])
		i = matrix[2][2] > matrix[1][1] ? 2 : 1;
	else
		i = matrix[2][2] > matrix[0][0] ? 2 : 0;
	const int j = (i + 1) % 3;
	const int k = (i + 2) % 3;
	btScalar diagonalI = matrix[i][i];
	btScalar diagonalJ = matrix[j][j];
	btScalar diagonalK = matrix[k][k];
	btScalar root;
	btScalar halfRoot;
	btMmdMath::quaternionRootParts(diagonalI, diagonalJ, diagonalK, false, &root, &halfRoot);
	const btScalar scale = mmdVelocityDiv(half, root);
	quaternion[i] = halfRoot;
	quaternion[3] = mmdVelocityMul(
		mmdVelocitySub(matrix[j][k], matrix[k][j]), scale);
	quaternion[j] = mmdVelocityMul(
		mmdVelocityAdd(matrix[j][i], matrix[i][j]), scale);
	quaternion[k] = mmdVelocityMul(
		mmdVelocityAdd(matrix[k][i], matrix[i][k]), scale);
}

static void mmdVelocityAxisAngleVc9(
	const btMatrix3x3& oldBasis, const btMatrix3x3& newBasis,
	btVector3& axis, btScalar& angle)
{
	const btMatrix3x3 relative =
		mmdVelocityRelativeBasisVc9(oldBasis, newBasis);
	btScalar quaternion[4];
	mmdVelocityQuaternionVc9(relative, quaternion);
	btScalar qx = quaternion[0];
	btScalar qy = quaternion[1];
	btScalar qz = quaternion[2];
	btScalar quaternionInverseLength;
	btScalar computedAngle;
	btMmdMath::quaternionAngle(quaternion, &quaternionInverseLength, &computedAngle);
	angle = computedAngle;

	qx = mmdVelocityMul(qx, quaternionInverseLength);
	qy = mmdVelocityMul(qy, quaternionInverseLength);
	qz = mmdVelocityMul(qz, quaternionInverseLength);
	btScalar axisLength2 = mmdVelocityAdd(
		mmdVelocityAdd(mmdVelocityMul(qx, qx), mmdVelocityMul(qy, qy)),
		mmdVelocityMul(qz, qz));
	const btScalar minimumAxisLength2 = btScalar(1.4210855e-14);
	if (axisLength2 < minimumAxisLength2)
	{
		axis.setValue(btScalar(1.0), btScalar(0.0), btScalar(0.0));
		return;
	}

	axis.setValue(qx, qy, qz);
	btMmdMath::normalize(axis, axisLength2);
}

static void mmdCalculateVelocityVc9(
	const btTransform& oldTransform, const btTransform& newTransform,
	btScalar timeStep, btVector3& linearVelocity, btVector3& angularVelocity)
{
	const btScalar inverseTime = mmdVelocityDiv(btScalar(1.0), timeStep);
	linearVelocity.setValue(
		mmdVelocityMul(mmdVelocitySub(newTransform.getOrigin()[0], oldTransform.getOrigin()[0]), inverseTime),
		mmdVelocityMul(mmdVelocitySub(newTransform.getOrigin()[1], oldTransform.getOrigin()[1]), inverseTime),
		mmdVelocityMul(mmdVelocitySub(newTransform.getOrigin()[2], oldTransform.getOrigin()[2]), inverseTime));

	btVector3 axis;
	btScalar angle;
	mmdVelocityAxisAngleVc9(
		oldTransform.getBasis(), newTransform.getBasis(), axis, angle);
	angularVelocity.setValue(
		mmdVelocityMul(mmdVelocityMul(axis[0], angle), inverseTime),
		mmdVelocityMul(mmdVelocityMul(axis[1], angle), inverseTime),
		mmdVelocityMul(mmdVelocityMul(axis[2], angle), inverseTime));
}
#endif

//'temporarily' global variables
btScalar	gDeactivationTime = btScalar(2.);
bool	gDisableDeactivation = false;
static int uniqueId = 0;

btRigidBody::btRigidBody(const btRigidBody::btRigidBodyConstructionInfo& constructionInfo)
{
	setupRigidBody(constructionInfo);
}

btRigidBody::btRigidBody(btScalar mass, btMotionState *motionState, btCollisionShape *collisionShape, const btVector3 &localInertia)
{
	btRigidBodyConstructionInfo cinfo(mass,motionState,collisionShape,localInertia);
	setupRigidBody(cinfo);
}

void	btRigidBody::setupRigidBody(const btRigidBody::btRigidBodyConstructionInfo& constructionInfo)
{

	m_internalType=CO_RIGID_BODY;

	m_linearVelocity.setValue(btScalar(0.0), btScalar(0.0), btScalar(0.0));
	m_angularVelocity.setValue(btScalar(0.),btScalar(0.),btScalar(0.));
	m_angularFactor.setValue(1,1,1);
	m_linearFactor.setValue(1,1,1);
	m_gravity.setValue(btScalar(0.0), btScalar(0.0), btScalar(0.0));
	m_gravity_acceleration.setValue(btScalar(0.0), btScalar(0.0), btScalar(0.0));
	m_totalForce.setValue(btScalar(0.0), btScalar(0.0), btScalar(0.0));
	m_totalTorque.setValue(btScalar(0.0), btScalar(0.0), btScalar(0.0)),
	m_linearDamping = btScalar(0.);
	m_angularDamping = btScalar(0.5);
	m_linearSleepingThreshold = constructionInfo.m_linearSleepingThreshold;
	m_angularSleepingThreshold = constructionInfo.m_angularSleepingThreshold;
	m_optionalMotionState = constructionInfo.m_motionState;
	m_contactSolverType = 0;
	m_frictionSolverType = 0;
	m_additionalDamping = constructionInfo.m_additionalDamping;
	m_additionalDampingFactor = constructionInfo.m_additionalDampingFactor;
	m_additionalLinearDampingThresholdSqr = constructionInfo.m_additionalLinearDampingThresholdSqr;
	m_additionalAngularDampingThresholdSqr = constructionInfo.m_additionalAngularDampingThresholdSqr;
	m_additionalAngularDampingFactor = constructionInfo.m_additionalAngularDampingFactor;

	if (m_optionalMotionState)
	{
		m_optionalMotionState->getWorldTransform(m_worldTransform);
	} else
	{
		m_worldTransform = constructionInfo.m_startWorldTransform;
	}

	m_interpolationWorldTransform = m_worldTransform;
	m_interpolationLinearVelocity.setValue(0,0,0);
	m_interpolationAngularVelocity.setValue(0,0,0);
	
	//moved to btCollisionObject
	m_friction = constructionInfo.m_friction;
	m_restitution = constructionInfo.m_restitution;

	setCollisionShape( constructionInfo.m_collisionShape );
	m_debugBodyId = uniqueId++;
	
	setMassProps(constructionInfo.m_mass, constructionInfo.m_localInertia);
    setDamping(constructionInfo.m_linearDamping, constructionInfo.m_angularDamping);
	updateInertiaTensor();

}

void btRigidBody::predictIntegratedTransform(btScalar timeStep,btTransform& predictedTransform) 
{
	btTransformUtil::integrateTransform(m_worldTransform,m_linearVelocity,m_angularVelocity,timeStep,predictedTransform);
}

void			btRigidBody::saveKinematicState(btScalar timeStep)
{
	//todo: clamp to some (user definable) safe minimum timestep, to limit maximum angular/linear velocities
	if (timeStep != btScalar(0.))
	{
		//if we use motionstate to synchronize world transforms, get the new kinematic/animated world transform
		if (getMotionState())
			getMotionState()->getWorldTransform(m_worldTransform);
		btVector3 linVel,angVel;
		
#if defined(_MSC_VER) && defined(_M_IX86)
		mmdCalculateVelocityVc9(m_interpolationWorldTransform,m_worldTransform,timeStep,m_linearVelocity,m_angularVelocity);
#else
		btTransformUtil::calculateVelocity(m_interpolationWorldTransform,m_worldTransform,timeStep,m_linearVelocity,m_angularVelocity);
#endif
		m_interpolationLinearVelocity = m_linearVelocity;
		m_interpolationAngularVelocity = m_angularVelocity;
		m_interpolationWorldTransform = m_worldTransform;
		//printf("angular = %f %f %f\n",m_angularVelocity.getX(),m_angularVelocity.getY(),m_angularVelocity.getZ());
	}
}
	
void	btRigidBody::getAabb(btVector3& aabbMin,btVector3& aabbMax) const
{
	getCollisionShape()->getAabb(m_worldTransform,aabbMin,aabbMax);
}

void btRigidBody::setGravity(const btVector3& acceleration) 
{
	if (m_inverseMass != btScalar(0.0))
	{
		m_gravity = acceleration * (btScalar(1.0) / m_inverseMass);
	}
	m_gravity_acceleration = acceleration;
}

void btRigidBody::setDamping(btScalar lin_damping, btScalar ang_damping)
{
	m_linearDamping = GEN_clamped(lin_damping, (btScalar)btScalar(0.0), (btScalar)btScalar(1.0));
	m_angularDamping = GEN_clamped(ang_damping, (btScalar)btScalar(0.0), (btScalar)btScalar(1.0));
}

///applyDamping damps the velocity, using the given m_linearDamping and m_angularDamping
void			btRigidBody::applyDamping(btScalar timeStep)
{
	//On new damping: see discussion/issue report here: http://code.google.com/p/bullet/issues/detail?id=74
	//todo: do some performance comparisons (but other parts of the engine are probably bottleneck anyway

//#define USE_OLD_DAMPING_METHOD 1
#ifdef USE_OLD_DAMPING_METHOD
	m_linearVelocity *= GEN_clamped((btScalar(1.) - timeStep * m_linearDamping), (btScalar)btScalar(0.0), (btScalar)btScalar(1.0));
	m_angularVelocity *= GEN_clamped((btScalar(1.) - timeStep * m_angularDamping), (btScalar)btScalar(0.0), (btScalar)btScalar(1.0));
#elif defined(_MSC_VER) && defined(_M_IX86)
	// MMD 0x4EF590 (VC9): the damping factor pow(1-d, dt) is evaluated by
	// the x87 CRT and stays EXTENDED while each velocity component is
	// multiplied and rounded to float exactly once at the store. Emulate
	// with a double pow and single-rounding multiplies (powf + float mul
	// rounds the factor once more and drifts 1 ULP, e.g. body 15 vel.y
	// BFCFD3D9 -> BFCFD3D8 at frame 3 of the sample scene).
	{
		const double linFactor = pow(1.0 - (double)m_linearDamping,
		                             (double)timeStep);
		const double angFactor = pow(1.0 - (double)m_angularDamping,
		                             (double)timeStep);
		m_linearVelocity.setValue(
			(btScalar)((double)m_linearVelocity.getX() * linFactor),
			(btScalar)((double)m_linearVelocity.getY() * linFactor),
			(btScalar)((double)m_linearVelocity.getZ() * linFactor));
		m_angularVelocity.setValue(
			(btScalar)((double)m_angularVelocity.getX() * angFactor),
			(btScalar)((double)m_angularVelocity.getY() * angFactor),
			(btScalar)((double)m_angularVelocity.getZ() * angFactor));
	}
#else
	m_linearVelocity *= btPow(btScalar(1)-m_linearDamping, timeStep);
	m_angularVelocity *= btPow(btScalar(1)-m_angularDamping, timeStep);
#endif

	if (m_additionalDamping)
	{
		//Additional damping can help avoiding lowpass jitter motion, help stability for ragdolls etc.
		//Such damping is undesirable, so once the overall simulation quality of the rigid body dynamics system has improved, this should become obsolete
		if ((m_angularVelocity.length2() < m_additionalAngularDampingThresholdSqr) &&
			(m_linearVelocity.length2() < m_additionalLinearDampingThresholdSqr))
		{
			m_angularVelocity *= m_additionalDampingFactor;
			m_linearVelocity *= m_additionalDampingFactor;
		}
	

		btScalar speed = m_linearVelocity.length();
		if (speed < m_linearDamping)
		{
			btScalar dampVel = btScalar(0.005);
			if (speed > dampVel)
			{
				btVector3 dir = m_linearVelocity.normalized();
				m_linearVelocity -=  dir * dampVel;
			} else
			{
				m_linearVelocity.setValue(btScalar(0.),btScalar(0.),btScalar(0.));
			}
		}

		btScalar angSpeed = m_angularVelocity.length();
		if (angSpeed < m_angularDamping)
		{
			btScalar angDampVel = btScalar(0.005);
			if (angSpeed > angDampVel)
			{
				btVector3 dir = m_angularVelocity.normalized();
				m_angularVelocity -=  dir * angDampVel;
			} else
			{
				m_angularVelocity.setValue(btScalar(0.),btScalar(0.),btScalar(0.));
			}
		}
	}
}

void btRigidBody::applyGravity()
{
	if (isStaticOrKinematicObject())
		return;
	
	applyCentralForce(m_gravity);	

}

void btRigidBody::proceedToTransform(const btTransform& newTrans)
{
	setCenterOfMassTransform( newTrans );
}
	

void btRigidBody::setMassProps(btScalar mass, const btVector3& inertia)
{
	if (mass == btScalar(0.))
	{
		m_collisionFlags |= btCollisionObject::CF_STATIC_OBJECT;
		m_inverseMass = btScalar(0.);
	} else
	{
		m_collisionFlags &= (~btCollisionObject::CF_STATIC_OBJECT);
		m_inverseMass = btScalar(1.0) / mass;
	}
	
	m_invInertiaLocal.setValue(inertia.x() != btScalar(0.0) ? btScalar(1.0) / inertia.x(): btScalar(0.0),
				   inertia.y() != btScalar(0.0) ? btScalar(1.0) / inertia.y(): btScalar(0.0),
				   inertia.z() != btScalar(0.0) ? btScalar(1.0) / inertia.z(): btScalar(0.0));

}

	

void btRigidBody::updateInertiaTensor() 
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	{
		static int done = 0;
		if (!done)
		{
			done = 1;
			if (getenv("OPENMMD_LOG_INERTIA_OFFSET"))
			{
				FILE* f = fopen(getenv("OPENMMD_LOG_INERTIA_OFFSET"), "w");
				if (f)
				{
					fprintf(f, "invInertiaTensorWorld %08X invInertiaLocal %08X "
					           "linearVelocity %08X angularVelocity %08X\n",
						(unsigned)((unsigned char*)&m_invInertiaTensorWorld - (unsigned char*)this),
						(unsigned)((unsigned char*)&m_invInertiaLocal - (unsigned char*)this),
						(unsigned)((unsigned char*)&m_linearVelocity - (unsigned char*)this),
						(unsigned)((unsigned char*)&m_angularVelocity - (unsigned char*)this));
					fclose(f);
				}
			}
		}
	}
#endif
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
    m_invInertiaTensorWorld = btMmdMath::inverseInertiaTensor(
        m_worldTransform.getBasis(), m_invInertiaLocal);
#elif defined(_MSC_VER) && defined(_M_IX86)
	m_invInertiaTensorWorld = mmdInertiaTensorWorldVc9(
		m_worldTransform.getBasis(), m_invInertiaLocal);
#else
	m_invInertiaTensorWorld = m_worldTransform.getBasis().scaled(m_invInertiaLocal) * m_worldTransform.getBasis().transpose();
#endif
}

void btRigidBody::integrateVelocities(btScalar step) 
{
	if (isStaticOrKinematicObject())
		return;

	m_linearVelocity += m_totalForce * (m_inverseMass * step);
	m_angularVelocity += m_invInertiaTensorWorld * m_totalTorque * step;

#define MAX_ANGVEL SIMD_HALF_PI
	/// clamp angular velocity. collision calculations will fail on higher angular velocities	
	btScalar angvel = m_angularVelocity.length();
	if (angvel*step > MAX_ANGVEL)
	{
		m_angularVelocity *= (MAX_ANGVEL/step) /angvel;
	}

}

btQuaternion btRigidBody::getOrientation() const
{
		btQuaternion orn;
		m_worldTransform.getBasis().getRotation(orn);
		return orn;
}
	
	
void btRigidBody::setCenterOfMassTransform(const btTransform& xform)
{

	if (isStaticOrKinematicObject())
	{
		m_interpolationWorldTransform = m_worldTransform;
	} else
	{
		m_interpolationWorldTransform = xform;
	}
	m_interpolationLinearVelocity = getLinearVelocity();
	m_interpolationAngularVelocity = getAngularVelocity();
	m_worldTransform = xform;
	updateInertiaTensor();
}

bool btRigidBody::checkCollideWithOverride(btCollisionObject* co)
{
	btRigidBody* otherRb = btRigidBody::upcast(co);
	if (!otherRb)
		return true;

	for (int i = 0; i < m_constraintRefs.size(); ++i)
	{
		btTypedConstraint* c = m_constraintRefs[i];
		if (&c->getRigidBodyA() == otherRb || &c->getRigidBodyB() == otherRb)
			return false;
	}

	return true;
}

void btRigidBody::addConstraintRef(btTypedConstraint* c)
{
	int index = m_constraintRefs.findLinearSearch(c);
	if (index == m_constraintRefs.size())
		m_constraintRefs.push_back(c); 

	m_checkCollideWith = true;
}

void btRigidBody::removeConstraintRef(btTypedConstraint* c)
{
	m_constraintRefs.remove(c);
	m_checkCollideWith = m_constraintRefs.size() > 0;
}
