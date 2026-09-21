/*
Copyright (c) 2003-2006 Gino van den Bergen / Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/


#ifndef SIMD_TRANSFORM_UTIL_H
#define SIMD_TRANSFORM_UTIL_H

#include "btTransform.h"
#include <math.h>
#define ANGULAR_MOTION_THRESHOLD btScalar(0.5)*SIMD_HALF_PI

// MMD (VC9, x87) rounding boundaries for the rotation-integration chain.
// The additions below feed scalar-SSE stores, so a modern optimizer must not
// reassociate them; keep every rounding point explicit.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
static inline btScalar mmdTfAdd(btScalar a, btScalar b)
{
	volatile btScalar r = a + b;
	return r;
}
#if defined(_MSC_VER)
__declspec(noinline)
#endif
static inline btScalar mmdTfSub(btScalar a, btScalar b)
{
	volatile btScalar r = a - b;
	return r;
}
#if defined(_MSC_VER)
__declspec(noinline)
#endif
static inline btScalar mmdTfMul(btScalar a, btScalar b)
{
	volatile btScalar r = a * b;
	return r;
}




SIMD_FORCE_INLINE btVector3 btAabbSupport(const btVector3& halfExtents,const btVector3& supportDir)
{
	return btVector3(supportDir.x() < btScalar(0.0) ? -halfExtents.x() : halfExtents.x(),
      supportDir.y() < btScalar(0.0) ? -halfExtents.y() : halfExtents.y(),
      supportDir.z() < btScalar(0.0) ? -halfExtents.z() : halfExtents.z()); 
}






#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
/* Verbatim transcription of MMD 0x4EFD17..0x4EFE2D: the angular-speed
   length stays extended (fsqrt), the clamp and 0.001 comparisons use the
   once-rounded float copy, and the sync quotient comes from the x87 fsin
   instruction with axis components rounded once per fstp.  The Taylor
   branch keeps the original scalar-SSE scheduling. */
__declspec(noinline)
static void mmdSinCosAxisAsm(const float* w, float dt,
                             float* axis, float* pdw)
{
	const float thr = 0.78539819f;   /* 0x3F490FDB */
	const float half = 0.5f;         /* 0x3F000000 */
	const float tiny = 0.001f;       /* 0x3A83126F */
	const float tayc = 0.020833334f; /* 0x3CAAAAAB */
	float var48, qax, qay, qaz, qdw;
	__asm
	{
		mov edx, w
		fld dword ptr [edx+8]
		fld dword ptr [edx+4]
		fld dword ptr [edx]
		fld st
		fmul st, st(1)
		fld st(2)
		fmulp st(3), st
		faddp st(2), st
		fld st(2)
		fmulp st(3), st
		fxch st(1)
		faddp st(2), st
		fxch st(1)
		fsqrt
		fst dword ptr [var48]
		fld st
		fld dword ptr [dt]
		fmul st(1), st
		fld dword ptr [thr]
		fxch st(2)
		fcomip st, st(2)
		fstp st(1)
		jbe MMD_UNCLAMPED
		movss xmm2, dword ptr [thr]
		fstp st(1)
		divss xmm2, dword ptr [dt]
		movss dword ptr [var48], xmm2
		fld dword ptr [var48]
		jmp MMD_GOTFA
	MMD_UNCLAMPED:
		movss xmm2, dword ptr [var48]
		fxch st(1)
	MMD_GOTFA:
		movss xmm1, dword ptr [dt]
		fld dword ptr [half]
		comiss xmm2, dword ptr [tiny]
		jb MMD_TAYLOR
	MMD_SINPATH:
		fld st(1)
		fmul st, st(3)
		fmul st, st(1)
		fsin
		fdiv st, st(2)
		fmul st(4), st
		fxch st(4)
		fstp dword ptr [qax]
		fld dword ptr [edx+4]
		fmul st, st(4)
		fstp dword ptr [qay]
		fld dword ptr [edx+8]
		fmulp st(4), st
		fxch st(3)
		fstp dword ptr [qaz]
		jmp MMD_COSPART
	MMD_TAYLOR:
		fstp st(3)
		movaps xmm4, xmm1
		mulss xmm4, xmm1
		mulss xmm4, xmm1
		movaps xmm0, xmm1
		mulss xmm0, dword ptr [half]
		movss xmm1, dword ptr [edx]
		mulss xmm4, xmm2
		mulss xmm4, xmm2
		mulss xmm4, dword ptr [tayc]
		subss xmm0, xmm4
		mulss xmm1, xmm0
		movss dword ptr [qax], xmm1
		movss xmm1, dword ptr [edx+4]
		mulss xmm1, xmm0
		movss dword ptr [qay], xmm1
		movss xmm1, dword ptr [edx+8]
		mulss xmm1, xmm0
		movss dword ptr [qaz], xmm1
	MMD_COSPART:
		fmulp st(1), st
		fmulp st(1), st
		fcos
		fstp dword ptr [qdw]
	}
	axis[0] = qax; axis[1] = qay; axis[2] = qaz;
	*pdw = qdw;
}
#endif

#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
/* Env-gated (OPENMMD_DUMP_ITRANS=<path>) intermediate trace of the
   rotation-integration chain; body id read from the MMD rigid slot at
   btTransform+0x214 (the caller passes body+0x10 as curTrans). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
static int mmdITransEnabled(void)
{
	static int state = -1;
	if (state < 0)
		state = getenv("OPENMMD_DUMP_ITRANS") ? 1 : 0;
	return state;
}
static FILE* mmdITransStream(void)
{
	static FILE* stream = 0;
	if (!stream)
		stream = fopen(getenv("OPENMMD_DUMP_ITRANS"), "w");
	return stream;
}
static unsigned mmdBits(float v)
{
	unsigned bits;
	memcpy(&bits, &v, 4);
	return bits;
}
#define MTB(v) mmdBits(v)
static void mmdITransLog(int id, const btQuaternion& orn0,
                         const btVector3& angvel, const btVector3& linvel,
                         btScalar ax, btScalar ay, btScalar az, btScalar dw,
                         btScalar px, btScalar py, btScalar pz, btScalar pw,
                         btScalar invLen, const btQuaternion& q)
{
	if (!mmdITransEnabled()) return;
	FILE* s = mmdITransStream();
	if (!s) return;
	fprintf(s, "itr %d o %08X %08X %08X %08X w %08X %08X %08X "
	           "l %08X %08X %08X d %08X %08X %08X %08X "
	           "p %08X %08X %08X %08X i %08X q %08X %08X %08X %08X\n",
	        id, MTB(orn0.x()), MTB(orn0.y()), MTB(orn0.z()), MTB(orn0.w()),
	        MTB(angvel.x()), MTB(angvel.y()), MTB(angvel.z()),
	        MTB(linvel.x()), MTB(linvel.y()), MTB(linvel.z()),
	        MTB(ax), MTB(ay), MTB(az), MTB(dw),
	        MTB(px), MTB(py), MTB(pz), MTB(pw), MTB(invLen),
	        MTB(q.x()), MTB(q.y()), MTB(q.z()), MTB(q.w()));
}
#else
static inline void mmdITransLog(int, const btQuaternion&,
                                btScalar, btScalar, btScalar, btScalar,
                                btScalar, btScalar, btScalar, btScalar,
                                btScalar, const btQuaternion&) {}
#endif

/// Utils related to temporal transforms
class btTransformUtil
{

public:

	static void integrateTransform(const btTransform& curTrans,const btVector3& linvel,const btVector3& angvel,btScalar timeStep,btTransform& predictedTransform)
	{
		predictedTransform.setOrigin(curTrans.getOrigin() + linvel * timeStep);
//	#define QUATERNION_DERIVATIVE
	#ifdef QUATERNION_DERIVATIVE
		btQuaternion predictedOrn = curTrans.getRotation();
		predictedOrn += (angvel * predictedOrn) * (timeStep * btScalar(0.5));
		predictedOrn.normalize();
	#else
		//Exponential map
		//google for "Practical Parameterization of Rotations Using the Exponential Map", F. Sebastian Grassia
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
		/* MMD's VC9 build (0x4EFC90) keeps the angular speed length, the
		   sync quotient and the quaternion normalization factor on the x87
		   stack (extended) and rounds each to float exactly once:
		   - fAngle = fstp(sqrt((z^2+y^2)+x^2))
		   - sin path: axis_i = fstp(w_i * (sin((fAngle*dt)*0.5)/fAngle))
		   - cos path: dorn.w = fstp(cos(0.5*(dt*fAngle)))
		   - normalize: invLen = fstp(1/sqrt(((w^2+z^2)+y^2)+x^2)),
		     then one SSE multiply per component.
		   The Taylor branch and the Hamilton product stay scalar-SSE with
		   one rounding per operation, but with MMD's operand order (the x
		   row of the product groups the cross terms differently). */
		btScalar axisArr[3];
		btScalar dw;
		mmdSinCosAxisAsm((const float*)&angvel.x(), timeStep, axisArr, &dw);
		const btScalar ax = axisArr[0];
		const btScalar ay = axisArr[1];
		const btScalar az = axisArr[2];

		btQuaternion orn0 = curTrans.getRotation();
		const btScalar ox = orn0.x(), oy = orn0.y(),
			oz = orn0.z(), ow = orn0.w();
		/* Hamilton product dorn*orn0 with MMD's add tree: the x row sums
		   (o.z*d.y + o.w*d.x) + d.w*o.x first, the other rows match the
		   source spelling. */
		const btScalar px = mmdTfSub(
			mmdTfAdd(mmdTfAdd(mmdTfMul(oz, ay), mmdTfMul(ow, ax)),
				mmdTfMul(dw, ox)),
			mmdTfMul(oy, az));
		const btScalar py = mmdTfSub(
			mmdTfAdd(mmdTfAdd(mmdTfMul(oy, dw), mmdTfMul(ow, ay)),
				mmdTfMul(az, ox)),
			mmdTfMul(oz, ax));
		const btScalar pz = mmdTfSub(
			mmdTfAdd(mmdTfAdd(mmdTfMul(oz, dw), mmdTfMul(ow, az)),
				mmdTfMul(oy, ax)),
			mmdTfMul(ay, ox));
		const btScalar pw = mmdTfSub(
			mmdTfSub(mmdTfSub(mmdTfMul(ow, dw), mmdTfMul(ax, ox)),
				mmdTfMul(ay, oy)),
			mmdTfMul(az, oz));

		double lenD = (double)pw * pw;
		lenD += (double)pz * pz;
		lenD += (double)py * py;
		lenD += (double)px * px;
		const btScalar invLen = (btScalar)(1.0 / ::sqrt(lenD));
		btQuaternion predictedOrn(
			mmdTfMul(px, invLen), mmdTfMul(py, invLen),
			mmdTfMul(pz, invLen), mmdTfMul(pw, invLen));
		mmdITransLog(
			*(const int*)((const char*)&curTrans + 0x214),
			orn0, angvel, linvel, ax, ay, az, dw,
			px, py, pz, pw, invLen, predictedOrn);
#else
		btVector3 axis;
		btScalar	fAngle = angvel.length();
		//limit the angular motion
		if (fAngle*timeStep > ANGULAR_MOTION_THRESHOLD)
		{
			fAngle = ANGULAR_MOTION_THRESHOLD / timeStep;
		}

		if ( fAngle < btScalar(0.001) )
		{
			// use Taylor's expansions of sync function
			axis   = angvel*( btScalar(0.5)*timeStep-(timeStep*timeStep*timeStep)*(btScalar(0.020833333333))*fAngle*fAngle );
		}
		else
		{
			// sync(fAngle) = sin(c*fAngle)/t
			axis   = angvel*( btSin(btScalar(0.5)*fAngle*timeStep)/fAngle );
		}
		btQuaternion dorn (axis.x(),axis.y(),axis.z(),btCos( fAngle*timeStep*btScalar(0.5) ));
		btQuaternion orn0 = curTrans.getRotation();

		btQuaternion predictedOrn = dorn * orn0;
		predictedOrn.normalize();
#endif
	#endif
		predictedTransform.setRotation(predictedOrn);
	}

	static void	calculateVelocityQuaternion(const btVector3& pos0,const btVector3& pos1,const btQuaternion& orn0,const btQuaternion& orn1,btScalar timeStep,btVector3& linVel,btVector3& angVel)
	{
		linVel = (pos1 - pos0) / timeStep;
		btVector3 axis;
		btScalar  angle;
		if (orn0 != orn1)
		{
			calculateDiffAxisAngleQuaternion(orn0,orn1,axis,angle);
			angVel = axis * angle / timeStep;
		} else
		{
			angVel.setValue(0,0,0);
		}
	}

	static void calculateDiffAxisAngleQuaternion(const btQuaternion& orn0,const btQuaternion& orn1a,btVector3& axis,btScalar& angle)
	{
		btQuaternion orn1 = orn0.nearest(orn1a);
		btQuaternion dorn = orn1 * orn0.inverse();
		///floating point inaccuracy can lead to w component > 1..., which breaks 
		dorn.normalize();
		angle = dorn.getAngle();
		axis = btVector3(dorn.x(),dorn.y(),dorn.z());
		axis[3] = btScalar(0.);
		//check for axis length
		btScalar len = axis.length2();
		if (len < SIMD_EPSILON*SIMD_EPSILON)
			axis = btVector3(btScalar(1.),btScalar(0.),btScalar(0.));
		else
			axis /= btSqrt(len);
	}

	static void	calculateVelocity(const btTransform& transform0,const btTransform& transform1,btScalar timeStep,btVector3& linVel,btVector3& angVel)
	{
		linVel = (transform1.getOrigin() - transform0.getOrigin()) / timeStep;
		btVector3 axis;
		btScalar  angle;
		calculateDiffAxisAngle(transform0,transform1,axis,angle);
		angVel = axis * angle / timeStep;
	}

	static void calculateDiffAxisAngle(const btTransform& transform0,const btTransform& transform1,btVector3& axis,btScalar& angle)
	{
		btMatrix3x3 dmat = transform1.getBasis() * transform0.getBasis().inverse();
		btQuaternion dorn;
		dmat.getRotation(dorn);

		///floating point inaccuracy can lead to w component > 1..., which breaks 
		dorn.normalize();
		
		angle = dorn.getAngle();
		axis = btVector3(dorn.x(),dorn.y(),dorn.z());
		axis[3] = btScalar(0.);
		//check for axis length
		btScalar len = axis.length2();
		if (len < SIMD_EPSILON*SIMD_EPSILON)
			axis = btVector3(btScalar(1.),btScalar(0.),btScalar(0.));
		else
			axis /= btSqrt(len);
	}

};


///The btConvexSeparatingDistanceUtil can help speed up convex collision detection 
///by conservatively updating a cached separating distance/vector instead of re-calculating the closest distance
class	btConvexSeparatingDistanceUtil
{
	btQuaternion	m_ornA;
	btQuaternion	m_ornB;
	btVector3	m_posA;
	btVector3	m_posB;
	
	btVector3	m_separatingNormal;

	btScalar	m_boundingRadiusA;
	btScalar	m_boundingRadiusB;
	btScalar	m_separatingDistance;

public:

	btConvexSeparatingDistanceUtil(btScalar	boundingRadiusA,btScalar	boundingRadiusB)
		:m_boundingRadiusA(boundingRadiusA),
		m_boundingRadiusB(boundingRadiusB),
		m_separatingDistance(0.f)
	{
	}

	btScalar	getConservativeSeparatingDistance()
	{
		return m_separatingDistance;
	}

	void	updateSeparatingDistance(const btTransform& transA,const btTransform& transB)
	{
		const btVector3& toPosA = transA.getOrigin();
		const btVector3& toPosB = transB.getOrigin();
		btQuaternion toOrnA = transA.getRotation();
		btQuaternion toOrnB = transB.getRotation();

		if (m_separatingDistance>0.f)
		{
			

			btVector3 linVelA,angVelA,linVelB,angVelB;
			btTransformUtil::calculateVelocityQuaternion(m_posA,toPosA,m_ornA,toOrnA,btScalar(1.),linVelA,angVelA);
			btTransformUtil::calculateVelocityQuaternion(m_posB,toPosB,m_ornB,toOrnB,btScalar(1.),linVelB,angVelB);
			btScalar maxAngularProjectedVelocity = angVelA.length() * m_boundingRadiusA + angVelB.length() * m_boundingRadiusB;
			btVector3 relLinVel = (linVelB-linVelA);
			btScalar relLinVelocLength = (linVelB-linVelA).dot(m_separatingNormal);
			if (relLinVelocLength<0.f)
			{
				relLinVelocLength = 0.f;
			}
	
			btScalar	projectedMotion = maxAngularProjectedVelocity +relLinVelocLength;
			m_separatingDistance -= projectedMotion;
		}
	
		m_posA = toPosA;
		m_posB = toPosB;
		m_ornA = toOrnA;
		m_ornB = toOrnB;
	}

	void	initSeparatingDistance(const btVector3& separatingVector,btScalar separatingDistance,const btTransform& transA,const btTransform& transB)
	{
		m_separatingDistance = separatingDistance;

		if (m_separatingDistance>0.f)
		{
			m_separatingNormal = separatingVector;
			
			const btVector3& toPosA = transA.getOrigin();
			const btVector3& toPosB = transB.getOrigin();
			btQuaternion toOrnA = transA.getRotation();
			btQuaternion toOrnB = transB.getRotation();
			m_posA = toPosA;
			m_posB = toPosB;
			m_ornA = toOrnA;
			m_ornB = toOrnB;
		}
	}

};


#endif //SIMD_TRANSFORM_UTIL_H

