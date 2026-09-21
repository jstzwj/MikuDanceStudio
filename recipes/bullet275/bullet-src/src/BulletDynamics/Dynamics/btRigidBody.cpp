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
#include <cstdio>
#include <cstdlib>
#include "BulletCollision/CollisionShapes/btConvexShape.h"
#include "LinearMath/btMinMax.h"
#include "LinearMath/btTransformUtil.h"
#include "LinearMath/btMotionState.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#if defined(_MSC_VER) && defined(_M_IX86)
#include <xmmintrin.h>
extern "C" void __cdecl _CIacos(void);
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
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

#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
/* Verbatim transcription of MMD's btRigidBody::updateInertiaTensor
   (0x4F0590..0x4F095B): the nine invInertiaTensorWorld elements mix
   term orders and reuse stack slots in ways no fold-permutation of the
   stock scaled(basis)*transpose(basis) reproduces, so the instruction
   stream is copied exactly.  this (the btRigidBody) arrives in the
   body argument; all reads/writes are body-relative like the
   original. */
__declspec(naked)
static void mmdUpdateInertiaAsm(unsigned char* /*body*/)
{
	__asm
	{
		mov ecx, [esp+4]
		push ebp
		mov ebp, esp
		and esp, 0FFFFFFF0h
		sub esp, 60h
		movss xmm2, dword ptr [ecx+38h]
		movss xmm7, dword ptr [ecx+1B8h]
		movss xmm5, dword ptr [ecx+30h]
		movss xmm1, dword ptr [ecx+28h]
		movss xmm0, dword ptr [ecx+14h]
		movss xmm4, dword ptr [ecx+20h]
		movss xmm3, dword ptr [ecx+10h]
		movaps xmm6, xmm2
		mulss xmm6, xmm7
		movss [esp+0Ch], xmm6
		movss xmm7, dword ptr [ecx+1B4h]
		movss xmm6, dword ptr [ecx+34h]
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B0h]
		movss [esp+08h], xmm6
		movaps xmm6, xmm5
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B8h]
		movss [esp+10h], xmm6
		movss [esp+40h], xmm0
		movss xmm0, dword ptr [ecx+24h]
		movaps xmm6, xmm1
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B4h]
		movss [esp+18h], xmm6
		movss xmm6, dword ptr [ecx+24h]
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B0h]
		movss [esp+14h], xmm6
		movss [esp+44h], xmm0
		movss xmm0, dword ptr [ecx+34h]
		movss [esp+48h], xmm0
		movss xmm0, dword ptr [ecx+18h]
		movaps xmm6, xmm4
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B8h]
		movss [esp+1Ch], xmm6
		movaps xmm6, xmm0
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B4h]
		movss [esp+24h], xmm6
		movss xmm6, dword ptr [ecx+14h]
		mulss xmm6, xmm7
		movss xmm7, dword ptr [ecx+1B0h]
		movss [esp+28h], xmm6
		movaps xmm6, xmm3
		mulss xmm6, xmm7
		movss [esp+20h], xmm6
		movaps xmm6, xmm2
		mulss xmm6, [esp+0Ch]
		movss [esp+04h], xmm6
		movss xmm7, [esp+04h]
		movss xmm6, [esp+48h]
		mulss xmm6, [esp+08h]
		addss xmm7, xmm6
		movss [esp+04h], xmm7
		movaps xmm6, xmm5
		mulss xmm6, [esp+10h]
		addss xmm7, xmm6
		movss xmm6, [esp+44h]
		mulss xmm6, [esp+08h]
		movss [esp+04h], xmm6
		movss [esp+2Ch], xmm7
		movss xmm7, [esp+04h]
		movaps xmm6, xmm1
		mulss xmm6, [esp+0Ch]
		addss xmm7, xmm6
		movss [esp+04h], xmm7
		movaps xmm6, xmm4
		mulss xmm6, [esp+10h]
		addss xmm7, xmm6
		movss xmm6, [esp+40h]
		mulss xmm6, [esp+08h]
		movss [esp+08h], xmm6
		movss [esp+04h], xmm7
		movss xmm7, [esp+08h]
		movaps xmm6, xmm0
		mulss xmm6, [esp+0Ch]
		addss xmm7, xmm6
		movss [esp+08h], xmm7
		movaps xmm6, xmm3
		mulss xmm6, [esp+10h]
		addss xmm7, xmm6
		movss [esp+08h], xmm7
		movaps xmm6, xmm2
		mulss xmm6, [esp+18h]
		movss [esp+10h], xmm6
		movss xmm7, [esp+10h]
		movss xmm6, [esp+48h]
		mulss xmm6, [esp+14h]
		addss xmm7, xmm6
		movss [esp+10h], xmm7
		movaps xmm6, xmm5
		mulss xmm6, [esp+1Ch]
		addss xmm7, xmm6
		movss xmm6, [esp+44h]
		mulss xmm6, [esp+14h]
		mulss xmm5, [esp+20h]
		movss [esp+10h], xmm6
		movaps xmm6, xmm1
		mulss xmm6, [esp+18h]
		movss [esp+0Ch], xmm7
		movss xmm7, [esp+10h]
		addss xmm7, xmm6
		movss [esp+10h], xmm7
		movaps xmm6, xmm4
		mulss xmm6, [esp+1Ch]
		addss xmm7, xmm6
		movss xmm6, [esp+40h]
		mulss xmm6, [esp+14h]
		movss [esp+14h], xmm6
		movss [esp+10h], xmm7
		movss xmm7, [esp+14h]
		movaps xmm6, xmm0
		mulss xmm6, [esp+18h]
		addss xmm7, xmm6
		movaps xmm6, xmm3
		mulss xmm6, [esp+1Ch]
		movss [esp+14h], xmm7
		addss xmm7, xmm6
		movss xmm6, [esp+24h]
		mulss xmm2, xmm6
		addss xmm2, xmm5
		movss xmm5, [esp+48h]
		mulss xmm1, xmm6
		movss [esp+1Ch], xmm7
		movss xmm7, [esp+28h]
		mulss xmm5, xmm7
		addss xmm2, xmm5
		movss xmm5, [esp+44h]
		mulss xmm5, xmm7
		addss xmm1, xmm5
		movss xmm5, [esp+20h]
		mulss xmm0, xmm6
		mulss xmm3, xmm5
		addss xmm0, xmm3
		movss xmm3, [esp+40h]
		mulss xmm4, xmm5
		addss xmm1, xmm4
		movss [esp+34h], xmm1
		movss xmm1, [esp+1Ch]
		movss [esp+40h], xmm1
		movss xmm1, [esp+10h]
		movss [esp+44h], xmm1
		movss xmm1, [esp+0Ch]
		movss [esp+48h], xmm1
		movss xmm1, [esp+08h]
		mulss xmm3, xmm7
		addss xmm0, xmm3
		movss [esp+50h], xmm1
		movss xmm1, [esp+04h]
		movss [esp+30h], xmm0
		xorps xmm0, xmm0
		mov eax, [esp+30h]
		movss [esp+54h], xmm1
		movss xmm1, [esp+2Ch]
		movss [esp+38h], xmm2
		movss [esp+3Ch], xmm0
		movss [esp+4Ch], xmm0
		movss [esp+58h], xmm1
		movss [esp+5Ch], xmm0
		mov [ecx+110h], eax
		mov edx, [esp+34h]
		mov eax, [esp+38h]
		mov [ecx+114h], edx
		mov edx, [esp+3Ch]
		mov [ecx+118h], eax
		mov eax, [esp+40h]
		mov [ecx+11Ch], edx
		mov edx, [esp+44h]
		mov [ecx+120h], eax
		mov eax, [esp+48h]
		mov [ecx+124h], edx
		mov edx, [esp+4Ch]
		mov [ecx+128h], eax
		mov eax, [esp+50h]
		mov [ecx+12Ch], edx
		mov edx, [esp+54h]
		mov [ecx+130h], eax
		mov eax, [esp+58h]
		mov [ecx+134h], edx
		mov edx, [esp+5Ch]
		mov [ecx+138h], eax
		mov [ecx+13Ch], edx
		mov esp, ebp
		pop ebp
		ret
	}
}
#endif

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
	const btScalar one = btScalar(1.0);
	const btScalar half = btScalar(0.5);
	btScalar trace = mmdVelocityAdd(
		mmdVelocityAdd(matrix[0][0], matrix[1][1]), matrix[2][2]);

	if (trace > zero)
	{
		btScalar root;
		btScalar halfRoot;
		__asm
		{
			fld trace
			fadd one
			fsqrt
			fst root
			fmul half
			fstp halfRoot
		}
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
	__asm
	{
		fld diagonalI
		fsub diagonalJ
		fsub diagonalK
		fadd one
		fsqrt
		fst root
		fmul half
		fstp halfRoot
	}
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
	// _CIacos is called from inline assembly below.  MSVC cannot see that call's
	// XMM clobbers, so these values must cross the boundary in memory.  VC9's
	// original code likewise reloads qx/qy/qz after _CIacos returns.
	volatile btScalar qx = quaternion[0];
	volatile btScalar qy = quaternion[1];
	volatile btScalar qz = quaternion[2];
	volatile btScalar qw = quaternion[3];
	btScalar quaternionInverseLength;
	btScalar computedAngle;
	__asm
	{
		fld qz
		fmul st, st
		fld qw
		fld st
		fmul st, st(1)
		faddp st(2), st
		fld qy
		fmul st, st
		faddp st(2), st
		fld qx
		fmul st, st
		faddp st(2), st
		fxch st(1)
		fsqrt
		fld1
		fdivrp st(1), st
		fst quaternionInverseLength
		fmulp st(1), st
		call _CIacos
		fadd st, st
		fstp computedAngle
	}
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

	btScalar axisX;
	btScalar axisY;
	btScalar axisZ;
	__asm
	{
		fld axisLength2
		fsqrt
		fld1
		fdivrp st(1), st
		fld qx
		fmul st, st(1)
		fstp axisX
		fld qy
		fmul st, st(1)
		fstp axisY
		fmul qz
		fstp axisZ
	}
	axis.setValue(axisX, axisY, axisZ);
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
	mmdUpdateInertiaAsm(reinterpret_cast<unsigned char*>(this));
	return;
#endif
#if defined(_MSC_VER) && defined(_M_IX86)
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
