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

//#define COMPUTE_IMPULSE_DENOM 1
//It is not necessary (redundant) to refresh contact manifolds, this refresh has been moved to the collision algorithms.

#include "btSequentialImpulseConstraintSolver.h"
#include "BulletCollision/NarrowPhaseCollision/btPersistentManifold.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "btContactConstraint.h"
#include "btSolve2LinearConstraint.h"
#include "btContactSolverInfo.h"
#include "LinearMath/btIDebugDraw.h"
#include "btJacobianEntry.h"
#include "LinearMath/btMinMax.h"
#include "BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#include <new>
#include "LinearMath/btStackAlloc.h"
#include "LinearMath/btQuickprof.h"
#include "btSolverBody.h"
#include "btSolverConstraint.h"
#include "LinearMath/btAlignedObjectArray.h"
#include <string.h> //for memset
#if defined(_MSC_VER) && defined(_M_IX86)
#include <xmmintrin.h>
#endif

// OpenMMD task#8: env-gated solver event trace for bit-exact alignment with
// the original exe. Set OPENMMD_DUMP_RESOLVE=<file> to record one JSON
// object per solver event of the first solveGroup call (mirrors
// scripts/ida_trace_solver_seq.py on the original side); without the
// variable the cost is one cached-bool branch per event.
#include <cstdlib>
#include <cstdio>
namespace {
FILE* g_openmmdLog = 0;
bool g_openmmdDisabled = false;
int g_openmmdSeq = 0;
int g_openmmdSolveCount = 0;
unsigned openmmdBits(float value) {
	unsigned bits; memcpy(&bits, &value, 4); return bits;
}
void openmmdHex3(FILE* f, const btVector3& v) {
	fprintf(f, "[\"%08X\",\"%08X\",\"%08X\"]",
	        openmmdBits(v.getX()), openmmdBits(v.getY()),
	        openmmdBits(v.getZ()));
}
int openmmdBodyId(const btRigidBody* body) {
	if (!body) return -1;
	int id = -1;
	memcpy(&id, (const char*)body + 0x224, 4);  // MMD rigid id slot
	return id;
}
int openmmdSolveLimit() {
	static int limit = -2;
	if (limit == -2) {
		const char* text = getenv("OPENMMD_DUMP_SOLVES");
		limit = (text && *text) ? atoi(text) : 1;
	}
	return limit;
}
bool openmmdLogOpen() {
	if (g_openmmdDisabled) return false;
	if (!g_openmmdLog) {
		const char* path = getenv("OPENMMD_DUMP_RESOLVE");
		if (!path || !*path) { g_openmmdDisabled = true; return false; }
		g_openmmdLog = fopen(path, "wb");
		if (!g_openmmdLog) { g_openmmdDisabled = true; return false; }
	}
	return true;
}
void openmmdLogResolve(const char* kind, int iter, int idx,
                       const btSolverBody& b1, const btSolverBody& b2,
                       const btSolverConstraint& c) {
	if (g_openmmdSolveCount > openmmdSolveLimit() || !openmmdLogOpen()) return;
	float push; memcpy(&push, &c.m_appliedPushImpulse, 4);
	float applied; memcpy(&applied, &c.m_appliedImpulse, 4);
	++g_openmmdSeq;
	fprintf(g_openmmdLog,
	        "{\"n\":%d,\"kind\":\"%s\",\"iter\":%d,\"j\":%d,"
	        "\"id_a\":%d,\"id_b\":%d,"
	        "\"a_dlin\":", g_openmmdSeq, kind, iter, idx,
	        openmmdBodyId(b1.m_originalBody),
	        openmmdBodyId(b2.m_originalBody));
	openmmdHex3(g_openmmdLog, b1.m_deltaLinearVelocity);
	fprintf(g_openmmdLog, ",\"a_dang\":");
	openmmdHex3(g_openmmdLog, b1.m_deltaAngularVelocity);
	fprintf(g_openmmdLog, ",\"b_dlin\":");
	openmmdHex3(g_openmmdLog, b2.m_deltaLinearVelocity);
	fprintf(g_openmmdLog, ",\"b_dang\":");
	openmmdHex3(g_openmmdLog, b2.m_deltaAngularVelocity);
	fprintf(g_openmmdLog,
	        ",\"push\":\"%08X\",\"applied\":\"%08X\","
	        "\"friction\":\"%08X\",\"jac\":\"%08X\","
	        "\"frictionIndex\":%d,\"cidA\":%d,\"cidB\":%d,"
	        "\"rhs\":\"%08X\",\"cfm\":\"%08X\","
	        "\"lower\":\"%08X\",\"upper\":\"%08X\",",
	        openmmdBits(push), openmmdBits(applied),
	        openmmdBits(c.m_friction), openmmdBits(c.m_jacDiagABInv),
	        c.m_frictionIndex, c.m_solverBodyIdA, c.m_solverBodyIdB,
	        openmmdBits(c.m_rhs), openmmdBits(c.m_cfm),
	        openmmdBits(c.m_lowerLimit), openmmdBits(c.m_upperLimit));
	fprintf(g_openmmdLog, ",");
	openmmdHex3(g_openmmdLog, c.m_relpos1CrossNormal);
	fprintf(g_openmmdLog, ",");
	openmmdHex3(g_openmmdLog, c.m_contactNormal);
	fprintf(g_openmmdLog, ",");
	openmmdHex3(g_openmmdLog, c.m_relpos2CrossNormal);
	fprintf(g_openmmdLog, ",");
	openmmdHex3(g_openmmdLog, c.m_angularComponentA);
	fprintf(g_openmmdLog, ",");
	openmmdHex3(g_openmmdLog, c.m_angularComponentB);
	fprintf(g_openmmdLog, "\n");
}
void openmmdLogJoint(int iter, int idx, const btTypedConstraint* constraint,
                     const btSolverBody& b1, const btSolverBody& b2) {
	if (g_openmmdSolveCount > openmmdSolveLimit() || !openmmdLogOpen()) return;
	const btRigidBody& ra = constraint->getRigidBodyA();
	const btRigidBody& rb = constraint->getRigidBodyB();
	++g_openmmdSeq;
	fprintf(g_openmmdLog,
	        "{\"n\":%d,\"kind\":\"joint\",\"iter\":%d,\"j\":%d,"
	        "\"id_a\":%d,\"id_b\":%d,"
	        "\"a_vel\":", g_openmmdSeq, iter, idx,
	        openmmdBodyId(&ra), openmmdBodyId(&rb));
	openmmdHex3(g_openmmdLog, ra.getLinearVelocity());
	fprintf(g_openmmdLog, ",\"a_avel\":");
	openmmdHex3(g_openmmdLog, ra.getAngularVelocity());
	fprintf(g_openmmdLog, ",\"b_vel\":");
	openmmdHex3(g_openmmdLog, rb.getLinearVelocity());
	fprintf(g_openmmdLog, ",\"b_avel\":");
	openmmdHex3(g_openmmdLog, rb.getAngularVelocity());
	fprintf(g_openmmdLog, ",\"a_dlin\":");
	openmmdHex3(g_openmmdLog, b1.m_deltaLinearVelocity);
	fprintf(g_openmmdLog, ",\"a_dang\":");
	openmmdHex3(g_openmmdLog, b1.m_deltaAngularVelocity);
	fprintf(g_openmmdLog, ",\"b_dlin\":");
	openmmdHex3(g_openmmdLog, b2.m_deltaLinearVelocity);
	fprintf(g_openmmdLog, ",\"b_dang\":");
	openmmdHex3(g_openmmdLog, b2.m_deltaAngularVelocity);
	fprintf(g_openmmdLog, "}\n");
}
void openmmdLogConvert(const btPersistentManifold* manifold, int j,
                       const btManifoldPoint& cp,
                       const btRigidBody* rb0, const btRigidBody* rb1,
                       const btVector3& rel1, const btVector3& rel2,
                       const btVector3& angCompA, const btVector3& angCompB,
                       const btContactSolverInfo& info,
                       const btSolverConstraint& sc) {
	if (g_openmmdSolveCount > openmmdSolveLimit() || !openmmdLogOpen()) return;
	++g_openmmdSeq;
	fprintf(g_openmmdLog,
	        "{\"n\":%d,\"kind\":\"conv\",\"j\":%d,"
	        "\"normal\":", g_openmmdSeq, j);
	openmmdHex3(g_openmmdLog, cp.m_normalWorldOnB);
	fprintf(g_openmmdLog, ",\"rel1\":");
	openmmdHex3(g_openmmdLog, rel1);
	fprintf(g_openmmdLog, ",\"rel2\":");
	openmmdHex3(g_openmmdLog, rel2);
	fprintf(g_openmmdLog, ",\"angCompA\":");
	openmmdHex3(g_openmmdLog, angCompA);
	fprintf(g_openmmdLog, ",\"angCompB\":");
	openmmdHex3(g_openmmdLog, angCompB);
	fprintf(g_openmmdLog, ",\"angCompA2\":");
	openmmdHex3(g_openmmdLog, sc.m_angularComponentA);
	fprintf(g_openmmdLog, ",\"angCompB2\":");
	openmmdHex3(g_openmmdLog, sc.m_angularComponentB);
	fprintf(g_openmmdLog, ",\"rcn1\":");
	openmmdHex3(g_openmmdLog, sc.m_relpos1CrossNormal);
	fprintf(g_openmmdLog, ",\"rcn2\":");
	openmmdHex3(g_openmmdLog, sc.m_relpos2CrossNormal);
	fprintf(g_openmmdLog,
	        ",\"invMass0\":\"%08X\",\"invMass1\":\"%08X\","
	        "\"dist\":\"%08X\",\"slop\":\"%08X\","
	        "\"erp\":\"%08X\",\"dt\":\"%08X\","
	        "\"restitution\":\"%08X\",\"friction\":\"%08X\","
	        "\"lifetime\":%d,\"jac\":\"%08X\",\"rhs\":\"%08X\"}\n",
	        openmmdBits(rb0 ? rb0->getInvMass() : 0.f),
	        openmmdBits(rb1 ? rb1->getInvMass() : 0.f),
	        openmmdBits(cp.getDistance()),
	        openmmdBits(info.m_linearSlop),
	        openmmdBits(info.m_erp),
	        openmmdBits(info.m_timeStep),
	        openmmdBits(cp.m_combinedRestitution),
	        openmmdBits(cp.m_combinedFriction),
	        cp.m_lifeTime,
	        openmmdBits(sc.m_jacDiagABInv),
	        openmmdBits(sc.m_rhs));
	if (rb0) { openmmdHex3(g_openmmdLog, rb0->getLinearVelocity()); openmmdHex3(g_openmmdLog, rb0->getAngularVelocity()); }
	else { fprintf(g_openmmdLog, "[0,0,0][0,0,0]"); }
	if (rb1) { openmmdHex3(g_openmmdLog, rb1->getLinearVelocity()); openmmdHex3(g_openmmdLog, rb1->getAngularVelocity()); }
	else { fprintf(g_openmmdLog, "[0,0,0][0,0,0]"); }
	fprintf(g_openmmdLog, "\n");
}
void openmmdLogWriteback(const btSolverBody& b) {
	if (g_openmmdSolveCount > openmmdSolveLimit() || !openmmdLogOpen()) return;
	++g_openmmdSeq;
	fprintf(g_openmmdLog,
	        "{\"n\":%d,\"kind\":\"writeback\",\"id\":%d,\"dlin\":",
	        g_openmmdSeq, openmmdBodyId(b.m_originalBody));
	openmmdHex3(g_openmmdLog, b.m_deltaLinearVelocity);
	fprintf(g_openmmdLog, ",\"dang\":");
	openmmdHex3(g_openmmdLog, b.m_deltaAngularVelocity);
	fprintf(g_openmmdLog, "}\n");
}
}  // namespace

#if defined(_MSC_VER) && defined(_M_IX86)
static SIMD_FORCE_INLINE btScalar mmdSolverDot120(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	__m128 sum = _mm_add_ss(
		_mm_mul_ss(_mm_set_ss(a1), _mm_set_ss(b1)),
		_mm_mul_ss(_mm_set_ss(a2), _mm_set_ss(b2)));
	sum = _mm_add_ss(sum, _mm_mul_ss(_mm_set_ss(a0), _mm_set_ss(b0)));
	return _mm_cvtss_f32(sum);
}

static SIMD_FORCE_INLINE btScalar mmdSolverMul(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdSolverAdd(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdSolverSub(btScalar a, btScalar b)
{
	return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}

static SIMD_FORCE_INLINE btScalar mmdSolverDot012(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	btScalar sum = mmdSolverAdd(
		mmdSolverMul(a0, b0), mmdSolverMul(a1, b1));
	return mmdSolverAdd(sum, mmdSolverMul(a2, b2));
}

static SIMD_FORCE_INLINE btScalar mmdSolverDot210(
	btScalar a0, btScalar b0, btScalar a1, btScalar b1,
	btScalar a2, btScalar b2)
{
	btScalar sum = mmdSolverAdd(
		mmdSolverMul(a2, b2), mmdSolverMul(a1, b1));
	return mmdSolverAdd(sum, mmdSolverMul(a0, b0));
}

static btVector3 mmdSolverAngularComponentVc9(
	const btMatrix3x3& matrix, const btVector3& axis,
	const btVector3& angularFactor)
{
	return btVector3(
		mmdSolverMul(mmdSolverDot120(
			matrix[0][0], axis[0], matrix[0][1], axis[1], matrix[0][2], axis[2]),
			angularFactor[0]),
		mmdSolverMul(mmdSolverDot120(
			matrix[1][0], axis[0], matrix[1][1], axis[1], matrix[1][2], axis[2]),
			angularFactor[1]),
		mmdSolverMul(mmdSolverDot120(
			matrix[2][0], axis[0], matrix[2][1], axis[1], matrix[2][2], axis[2]),
			angularFactor[2]));
}

// OpenMMD (MMD 0x4FD380 @0x4FD950): convertContact's angular components
// sum each matrix row as (m[i][2]*t.z + m[i][1]*t.y) + m[i][0]*t.x - the
// (z+y)+x lane order, unlike the joint-setup helper above (y+z)+x.
static btVector3 mmdSolverAngularComponent210(
	const btMatrix3x3& matrix, const btVector3& axis,
	const btVector3& angularFactor)
{
	return btVector3(
		mmdSolverMul(mmdSolverDot210(
			matrix[0][0], axis[0], matrix[0][1], axis[1], matrix[0][2], axis[2]),
			angularFactor[0]),
		mmdSolverMul(mmdSolverDot210(
			matrix[1][0], axis[0], matrix[1][1], axis[1], matrix[1][2], axis[2]),
			angularFactor[1]),
		mmdSolverMul(mmdSolverDot210(
			matrix[2][0], axis[0], matrix[2][1], axis[1], matrix[2][2], axis[2]),
			angularFactor[2]));
}

static btScalar mmdSolverJacDiagVc9(
	const btMatrix3x3& inertiaA, btScalar inverseMassA,
	const btMatrix3x3& inertiaB, btScalar inverseMassB,
	const btVector3& normal, const btVector3& relpos1,
	const btVector3& relpos2)
{
	const btVector3 angularA(
		mmdSolverDot012(inertiaA[0][0], relpos1[0], inertiaA[0][1], relpos1[1], inertiaA[0][2], relpos1[2]),
		mmdSolverDot012(inertiaA[1][0], relpos1[0], inertiaA[1][1], relpos1[1], inertiaA[1][2], relpos1[2]),
		mmdSolverDot012(inertiaA[2][0], relpos1[0], inertiaA[2][1], relpos1[1], inertiaA[2][2], relpos1[2]));
	const btVector3 angularB(
		mmdSolverDot012(inertiaB[0][0], relpos2[0], inertiaB[0][1], relpos2[1], inertiaB[0][2], relpos2[2]),
		mmdSolverDot012(inertiaB[1][0], relpos2[0], inertiaB[1][1], relpos2[1], inertiaB[1][2], relpos2[2]),
		mmdSolverDot210(inertiaB[2][0], relpos2[0], inertiaB[2][1], relpos2[1], inertiaB[2][2], relpos2[2]));
	const btVector3 linearA(
		mmdSolverMul(normal[0], inverseMassA),
		mmdSolverMul(normal[1], inverseMassA),
		mmdSolverMul(normal[2], inverseMassA));
	const btVector3 linearB(
		mmdSolverMul(normal[0], inverseMassB),
		mmdSolverMul(normal[1], inverseMassB),
		mmdSolverMul(normal[2], inverseMassB));

	// Exact addss chain at MMD 0x4FF9E4..0x4FFAAE.  VC9 flattened
	// the four source-level dot products and scheduled their terms as below.
	btScalar sum = mmdSolverMul(normal[1], linearA[1]);
	sum = mmdSolverAdd(sum, mmdSolverMul(normal[2], linearA[2]));
	sum = mmdSolverAdd(sum, mmdSolverMul(normal[0], linearA[0]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos1[1], angularA[1]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos1[2], angularA[2]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos1[0], angularA[0]));
	sum = mmdSolverAdd(sum, mmdSolverMul(normal[1], linearB[1]));
	sum = mmdSolverAdd(sum, mmdSolverMul(normal[2], linearB[2]));
	sum = mmdSolverAdd(sum, mmdSolverMul(normal[0], linearB[0]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos2[0], angularB[0]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos2[2], angularB[2]));
	sum = mmdSolverAdd(sum, mmdSolverMul(relpos2[1], angularB[1]));
	return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(btScalar(1.)), _mm_set_ss(sum)));
}


static btScalar mmdSolverRhsVc9(
	const btRigidBody& rbA, const btRigidBody& rbB,
	const btVector3& normal, const btVector3& relpos1,
	const btVector3& relpos2, btScalar jacobian, btScalar positionalRhs)
{
	const btVector3& linearA = rbA.getLinearVelocity();
	const btVector3& angularA = rbA.getAngularVelocity();
	const btVector3& linearB = rbB.getLinearVelocity();
	const btVector3& angularB = rbB.getAngularVelocity();

	// Exact scalar instruction order at MMD 0x4FFAB7..0x4FFB7F.
	btScalar relVelocity = mmdSolverMul(angularB[2], relpos2[2]);
	relVelocity = mmdSolverSub(relVelocity, mmdSolverMul(normal[2], linearB[2]));
	relVelocity = mmdSolverSub(relVelocity, mmdSolverMul(normal[1], linearB[1]));
	relVelocity = mmdSolverAdd(relVelocity, mmdSolverMul(angularB[1], relpos2[1]));
	relVelocity = mmdSolverSub(relVelocity, mmdSolverMul(normal[0], linearB[0]));
	relVelocity = mmdSolverAdd(relVelocity, mmdSolverMul(angularB[0], relpos2[0]));

	btScalar bodyA = mmdSolverMul(angularA[0], relpos1[0]);
	bodyA = mmdSolverAdd(bodyA, mmdSolverMul(angularA[1], relpos1[1]));
	bodyA = mmdSolverAdd(bodyA, mmdSolverMul(angularA[2], relpos1[2]));
	bodyA = mmdSolverAdd(bodyA, mmdSolverMul(linearA[0], normal[0]));
	bodyA = mmdSolverAdd(bodyA, mmdSolverMul(linearA[1], normal[1]));
	bodyA = mmdSolverAdd(bodyA, mmdSolverMul(linearA[2], normal[2]));
	relVelocity = mmdSolverAdd(relVelocity, bodyA);

	const btScalar velocityImpulse = mmdSolverMul(
		mmdSolverSub(-btScalar(0.), relVelocity), jacobian);
	const btScalar penetrationImpulse = mmdSolverMul(jacobian, positionalRhs);
	return mmdSolverAdd(velocityImpulse, penetrationImpulse);
}
#endif

int		gNumSplitImpulseRecoveries = 0;

btSequentialImpulseConstraintSolver::btSequentialImpulseConstraintSolver()
:m_btSeed2(0)
{

}

btSequentialImpulseConstraintSolver::~btSequentialImpulseConstraintSolver()
{
}

#ifdef USE_SIMD
#include <emmintrin.h>
#define vec_splat(x, e) _mm_shuffle_ps(x, x, _MM_SHUFFLE(e,e,e,e))
static inline __m128 _vmathVfDot3( __m128 vec0, __m128 vec1 )
{
	__m128 result = _mm_mul_ps( vec0, vec1);
	// OpenMMD (MMD 0x4FB790/0x4FBC00): VC9 sums the product lanes as
	// (z + y) + x, not (x + y) + z.
	return _mm_add_ps( _mm_add_ps( vec_splat( result, 2 ), vec_splat( result, 1 ) ), vec_splat( result, 0 ) );
}
#endif//USE_SIMD

// Project Gauss Seidel or the equivalent Sequential Impulse
void btSequentialImpulseConstraintSolver::resolveSingleConstraintRowGenericSIMD(btSolverBody& body1,btSolverBody& body2,const btSolverConstraint& c)
{
#ifdef USE_SIMD
	__m128 cpAppliedImp = _mm_set1_ps(c.m_appliedImpulse);
	__m128	lowerLimit1 = _mm_set1_ps(c.m_lowerLimit);
	__m128	upperLimit1 = _mm_set1_ps(c.m_upperLimit);
	__m128 deltaImpulse = _mm_sub_ps(_mm_set1_ps(c.m_rhs), _mm_mul_ps(_mm_set1_ps(c.m_appliedImpulse),_mm_set1_ps(c.m_cfm)));
	__m128 deltaVel1Dotn	=	_mm_add_ps(_vmathVfDot3(c.m_contactNormal.mVec128,body1.m_deltaLinearVelocity.mVec128), _vmathVfDot3(c.m_relpos1CrossNormal.mVec128,body1.m_deltaAngularVelocity.mVec128));
	__m128 deltaVel2Dotn	=	_mm_sub_ps(_vmathVfDot3(c.m_relpos2CrossNormal.mVec128,body2.m_deltaAngularVelocity.mVec128),_vmathVfDot3((c.m_contactNormal).mVec128,body2.m_deltaLinearVelocity.mVec128));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel1Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel2Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	btSimdScalar sum = _mm_add_ps(cpAppliedImp,deltaImpulse);
	btSimdScalar resultLowerLess,resultUpperLess;
	resultLowerLess = _mm_cmplt_ps(sum,lowerLimit1);
	resultUpperLess = _mm_cmplt_ps(sum,upperLimit1);
	__m128 lowMinApplied = _mm_sub_ps(lowerLimit1,cpAppliedImp);
	deltaImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowMinApplied), _mm_andnot_ps(resultLowerLess, deltaImpulse) );
	c.m_appliedImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowerLimit1), _mm_andnot_ps(resultLowerLess, sum) );
	__m128 upperMinApplied = _mm_sub_ps(upperLimit1,cpAppliedImp);
	deltaImpulse = _mm_or_ps( _mm_and_ps(resultUpperLess, deltaImpulse), _mm_andnot_ps(resultUpperLess, upperMinApplied) );
	c.m_appliedImpulse = _mm_or_ps( _mm_and_ps(resultUpperLess, c.m_appliedImpulse), _mm_andnot_ps(resultUpperLess, upperLimit1) );
	__m128	linearComponentA = _mm_mul_ps(c.m_contactNormal.mVec128,body1.m_invMass.mVec128);
	__m128	linearComponentB = _mm_mul_ps((c.m_contactNormal).mVec128,body2.m_invMass.mVec128);
	__m128 impulseMagnitude = deltaImpulse;
	body1.m_deltaLinearVelocity.mVec128 = _mm_add_ps(body1.m_deltaLinearVelocity.mVec128,_mm_mul_ps(linearComponentA,impulseMagnitude));
	body1.m_deltaAngularVelocity.mVec128 = _mm_add_ps(body1.m_deltaAngularVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentA.mVec128,impulseMagnitude));
	body2.m_deltaLinearVelocity.mVec128 = _mm_sub_ps(body2.m_deltaLinearVelocity.mVec128,_mm_mul_ps(linearComponentB,impulseMagnitude));
	body2.m_deltaAngularVelocity.mVec128 = _mm_add_ps(body2.m_deltaAngularVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentB.mVec128,impulseMagnitude));
#else
	resolveSingleConstraintRowGeneric(body1,body2,c);
#endif
}

// Project Gauss Seidel or the equivalent Sequential Impulse
 void btSequentialImpulseConstraintSolver::resolveSingleConstraintRowGeneric(btSolverBody& body1,btSolverBody& body2,const btSolverConstraint& c)
{
	btScalar deltaImpulse = c.m_rhs-btScalar(c.m_appliedImpulse)*c.m_cfm;
	const btScalar deltaVel1Dotn	=	c.m_contactNormal.dot(body1.m_deltaLinearVelocity) 	+ c.m_relpos1CrossNormal.dot(body1.m_deltaAngularVelocity);
	const btScalar deltaVel2Dotn	=	-c.m_contactNormal.dot(body2.m_deltaLinearVelocity) + c.m_relpos2CrossNormal.dot(body2.m_deltaAngularVelocity);

//	const btScalar delta_rel_vel	=	deltaVel1Dotn-deltaVel2Dotn;
	deltaImpulse	-=	deltaVel1Dotn*c.m_jacDiagABInv;
	deltaImpulse	-=	deltaVel2Dotn*c.m_jacDiagABInv;

	const btScalar sum = btScalar(c.m_appliedImpulse) + deltaImpulse;
	if (sum < c.m_lowerLimit)
	{
		deltaImpulse = c.m_lowerLimit-c.m_appliedImpulse;
		c.m_appliedImpulse = c.m_lowerLimit;
	}
	else if (sum > c.m_upperLimit) 
	{
		deltaImpulse = c.m_upperLimit-c.m_appliedImpulse;
		c.m_appliedImpulse = c.m_upperLimit;
	}
	else
	{
		c.m_appliedImpulse = sum;
	}
		body1.applyImpulse(c.m_contactNormal*body1.m_invMass,c.m_angularComponentA,deltaImpulse);
		body2.applyImpulse(-c.m_contactNormal*body2.m_invMass,c.m_angularComponentB,deltaImpulse);
}

 void btSequentialImpulseConstraintSolver::resolveSingleConstraintRowLowerLimitSIMD(btSolverBody& body1,btSolverBody& body2,const btSolverConstraint& c)
{
#ifdef USE_SIMD
	__m128 cpAppliedImp = _mm_set1_ps(c.m_appliedImpulse);
	__m128	lowerLimit1 = _mm_set1_ps(c.m_lowerLimit);
	__m128	upperLimit1 = _mm_set1_ps(c.m_upperLimit);
	__m128 deltaImpulse = _mm_sub_ps(_mm_set1_ps(c.m_rhs), _mm_mul_ps(_mm_set1_ps(c.m_appliedImpulse),_mm_set1_ps(c.m_cfm)));
	__m128 deltaVel1Dotn	=	_mm_add_ps(_vmathVfDot3(c.m_contactNormal.mVec128,body1.m_deltaLinearVelocity.mVec128), _vmathVfDot3(c.m_relpos1CrossNormal.mVec128,body1.m_deltaAngularVelocity.mVec128));
	__m128 deltaVel2Dotn	=	_mm_sub_ps(_vmathVfDot3(c.m_relpos2CrossNormal.mVec128,body2.m_deltaAngularVelocity.mVec128),_vmathVfDot3((c.m_contactNormal).mVec128,body2.m_deltaLinearVelocity.mVec128));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel1Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel2Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	btSimdScalar sum = _mm_add_ps(cpAppliedImp,deltaImpulse);
	btSimdScalar resultLowerLess,resultUpperLess;
	resultLowerLess = _mm_cmplt_ps(sum,lowerLimit1);
	resultUpperLess = _mm_cmplt_ps(sum,upperLimit1);
	__m128 lowMinApplied = _mm_sub_ps(lowerLimit1,cpAppliedImp);
	deltaImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowMinApplied), _mm_andnot_ps(resultLowerLess, deltaImpulse) );
	c.m_appliedImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowerLimit1), _mm_andnot_ps(resultLowerLess, sum) );
	__m128	linearComponentA = _mm_mul_ps(c.m_contactNormal.mVec128,body1.m_invMass.mVec128);
	__m128	linearComponentB = _mm_mul_ps((c.m_contactNormal).mVec128,body2.m_invMass.mVec128);
	__m128 impulseMagnitude = deltaImpulse;
	body1.m_deltaLinearVelocity.mVec128 = _mm_add_ps(body1.m_deltaLinearVelocity.mVec128,_mm_mul_ps(linearComponentA,impulseMagnitude));
	body1.m_deltaAngularVelocity.mVec128 = _mm_add_ps(body1.m_deltaAngularVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentA.mVec128,impulseMagnitude));
	body2.m_deltaLinearVelocity.mVec128 = _mm_sub_ps(body2.m_deltaLinearVelocity.mVec128,_mm_mul_ps(linearComponentB,impulseMagnitude));
	body2.m_deltaAngularVelocity.mVec128 = _mm_add_ps(body2.m_deltaAngularVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentB.mVec128,impulseMagnitude));
#else
	resolveSingleConstraintRowLowerLimit(body1,body2,c);
#endif
}

// Project Gauss Seidel or the equivalent Sequential Impulse
 void btSequentialImpulseConstraintSolver::resolveSingleConstraintRowLowerLimit(btSolverBody& body1,btSolverBody& body2,const btSolverConstraint& c)
{
	btScalar deltaImpulse = c.m_rhs-btScalar(c.m_appliedImpulse)*c.m_cfm;
	const btScalar deltaVel1Dotn	=	c.m_contactNormal.dot(body1.m_deltaLinearVelocity) 	+ c.m_relpos1CrossNormal.dot(body1.m_deltaAngularVelocity);
	const btScalar deltaVel2Dotn	=	-c.m_contactNormal.dot(body2.m_deltaLinearVelocity) + c.m_relpos2CrossNormal.dot(body2.m_deltaAngularVelocity);

	deltaImpulse	-=	deltaVel1Dotn*c.m_jacDiagABInv;
	deltaImpulse	-=	deltaVel2Dotn*c.m_jacDiagABInv;
	const btScalar sum = btScalar(c.m_appliedImpulse) + deltaImpulse;
	if (sum < c.m_lowerLimit)
	{
		deltaImpulse = c.m_lowerLimit-c.m_appliedImpulse;
		c.m_appliedImpulse = c.m_lowerLimit;
	}
	else
	{
		c.m_appliedImpulse = sum;
	}
	body1.applyImpulse(c.m_contactNormal*body1.m_invMass,c.m_angularComponentA,deltaImpulse);
	body2.applyImpulse(-c.m_contactNormal*body2.m_invMass,c.m_angularComponentB,deltaImpulse);
}


void	btSequentialImpulseConstraintSolver::resolveSplitPenetrationImpulseCacheFriendly(
        btSolverBody& body1,
        btSolverBody& body2,
        const btSolverConstraint& c)
{
		if (c.m_rhsPenetration)
        {
			gNumSplitImpulseRecoveries++;
			btScalar deltaImpulse = c.m_rhsPenetration-btScalar(c.m_appliedPushImpulse)*c.m_cfm;
			const btScalar deltaVel1Dotn	=	c.m_contactNormal.dot(body1.m_pushVelocity) 	+ c.m_relpos1CrossNormal.dot(body1.m_turnVelocity);
			const btScalar deltaVel2Dotn	=	-c.m_contactNormal.dot(body2.m_pushVelocity) + c.m_relpos2CrossNormal.dot(body2.m_turnVelocity);

			deltaImpulse	-=	deltaVel1Dotn*c.m_jacDiagABInv;
			deltaImpulse	-=	deltaVel2Dotn*c.m_jacDiagABInv;
			const btScalar sum = btScalar(c.m_appliedPushImpulse) + deltaImpulse;
			if (sum < c.m_lowerLimit)
			{
				deltaImpulse = c.m_lowerLimit-c.m_appliedPushImpulse;
				c.m_appliedPushImpulse = c.m_lowerLimit;
			}
			else
			{
				c.m_appliedPushImpulse = sum;
			}
			body1.internalApplyPushImpulse(c.m_contactNormal*body1.m_invMass,c.m_angularComponentA,deltaImpulse);
			body2.internalApplyPushImpulse(-c.m_contactNormal*body2.m_invMass,c.m_angularComponentB,deltaImpulse);
        }
}

 void btSequentialImpulseConstraintSolver::resolveSplitPenetrationSIMD(btSolverBody& body1,btSolverBody& body2,const btSolverConstraint& c)
{
#ifdef USE_SIMD
	if (!c.m_rhsPenetration)
		return;

	gNumSplitImpulseRecoveries++;

	__m128 cpAppliedImp = _mm_set1_ps(c.m_appliedPushImpulse);
	__m128	lowerLimit1 = _mm_set1_ps(c.m_lowerLimit);
	__m128	upperLimit1 = _mm_set1_ps(c.m_upperLimit);
	__m128 deltaImpulse = _mm_sub_ps(_mm_set1_ps(c.m_rhsPenetration), _mm_mul_ps(_mm_set1_ps(c.m_appliedPushImpulse),_mm_set1_ps(c.m_cfm)));
	__m128 deltaVel1Dotn	=	_mm_add_ps(_vmathVfDot3(c.m_contactNormal.mVec128,body1.m_pushVelocity.mVec128), _vmathVfDot3(c.m_relpos1CrossNormal.mVec128,body1.m_turnVelocity.mVec128));
	__m128 deltaVel2Dotn	=	_mm_sub_ps(_vmathVfDot3(c.m_relpos2CrossNormal.mVec128,body2.m_turnVelocity.mVec128),_vmathVfDot3((c.m_contactNormal).mVec128,body2.m_pushVelocity.mVec128));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel1Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	deltaImpulse	=	_mm_sub_ps(deltaImpulse,_mm_mul_ps(deltaVel2Dotn,_mm_set1_ps(c.m_jacDiagABInv)));
	btSimdScalar sum = _mm_add_ps(cpAppliedImp,deltaImpulse);
	btSimdScalar resultLowerLess,resultUpperLess;
	resultLowerLess = _mm_cmplt_ps(sum,lowerLimit1);
	resultUpperLess = _mm_cmplt_ps(sum,upperLimit1);
	__m128 lowMinApplied = _mm_sub_ps(lowerLimit1,cpAppliedImp);
	deltaImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowMinApplied), _mm_andnot_ps(resultLowerLess, deltaImpulse) );
	c.m_appliedImpulse = _mm_or_ps( _mm_and_ps(resultLowerLess, lowerLimit1), _mm_andnot_ps(resultLowerLess, sum) );
	__m128	linearComponentA = _mm_mul_ps(c.m_contactNormal.mVec128,body1.m_invMass.mVec128);
	__m128	linearComponentB = _mm_mul_ps((c.m_contactNormal).mVec128,body2.m_invMass.mVec128);
	__m128 impulseMagnitude = deltaImpulse;
	body1.m_pushVelocity.mVec128 = _mm_add_ps(body1.m_pushVelocity.mVec128,_mm_mul_ps(linearComponentA,impulseMagnitude));
	body1.m_turnVelocity.mVec128 = _mm_add_ps(body1.m_turnVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentA.mVec128,impulseMagnitude));
	body2.m_pushVelocity.mVec128 = _mm_sub_ps(body2.m_pushVelocity.mVec128,_mm_mul_ps(linearComponentB,impulseMagnitude));
	body2.m_turnVelocity.mVec128 = _mm_add_ps(body2.m_turnVelocity.mVec128 ,_mm_mul_ps(c.m_angularComponentB.mVec128,impulseMagnitude));
#else
	resolveSplitPenetrationImpulseCacheFriendly(body1,body2,c);
#endif
}



unsigned long btSequentialImpulseConstraintSolver::btRand2()
{
	m_btSeed2 = (1664525L*m_btSeed2 + 1013904223L) & 0xffffffff;
	return m_btSeed2;
}



//See ODE: adam's all-int straightforward(?) dRandInt (0..n-1)
int btSequentialImpulseConstraintSolver::btRandInt2 (int n)
{
	// seems good; xor-fold and modulus
	const unsigned long un = static_cast<unsigned long>(n);
	unsigned long r = btRand2();

	// note: probably more aggressive than it needs to be -- might be
	//       able to get away without one or two of the innermost branches.
	if (un <= 0x00010000UL) {
		r ^= (r >> 16);
		if (un <= 0x00000100UL) {
			r ^= (r >> 8);
			if (un <= 0x00000010UL) {
				r ^= (r >> 4);
				if (un <= 0x00000004UL) {
					r ^= (r >> 2);
					if (un <= 0x00000002UL) {
						r ^= (r >> 1);
					}
				}
			}
		}
	}

	return (int) (r % un);
}



void	btSequentialImpulseConstraintSolver::initSolverBody(btSolverBody* solverBody, btCollisionObject* collisionObject)
{
	btRigidBody* rb = collisionObject? btRigidBody::upcast(collisionObject) : 0;

	solverBody->m_deltaLinearVelocity.setValue(0.f,0.f,0.f);
	solverBody->m_deltaAngularVelocity.setValue(0.f,0.f,0.f);
	solverBody->m_pushVelocity.setValue(0.f,0.f,0.f);
	solverBody->m_turnVelocity.setValue(0.f,0.f,0.f);

	if (rb)
	{
		solverBody->m_invMass = btVector3(rb->getInvMass(),rb->getInvMass(),rb->getInvMass())*rb->getLinearFactor();
		solverBody->m_originalBody = rb;
		solverBody->m_angularFactor = rb->getAngularFactor();
	} else
	{
		solverBody->m_invMass.setValue(0,0,0);
		solverBody->m_originalBody = 0;
		solverBody->m_angularFactor.setValue(1,1,1);
	}
}





btScalar btSequentialImpulseConstraintSolver::restitutionCurve(btScalar rel_vel, btScalar restitution)
{
	btScalar rest = restitution * -rel_vel;
	return rest;
}



void	applyAnisotropicFriction(btCollisionObject* colObj,btVector3& frictionDirection);
void	applyAnisotropicFriction(btCollisionObject* colObj,btVector3& frictionDirection)
{
	if (colObj && colObj->hasAnisotropicFriction())
	{
		// transform to local coordinates
		btVector3 loc_lateral = frictionDirection * colObj->getWorldTransform().getBasis();
		const btVector3& friction_scaling = colObj->getAnisotropicFriction();
		//apply anisotropic friction
		loc_lateral *= friction_scaling;
		// ... and transform it back to global coordinates
		frictionDirection = colObj->getWorldTransform().getBasis() * loc_lateral;
	}
}



btSolverConstraint&	btSequentialImpulseConstraintSolver::addFrictionConstraint(const btVector3& normalAxis,int solverBodyIdA,int solverBodyIdB,int frictionIndex,btManifoldPoint& cp,const btVector3& rel_pos1,const btVector3& rel_pos2,btCollisionObject* colObj0,btCollisionObject* colObj1, btScalar relaxation)
{


	btRigidBody* body0=btRigidBody::upcast(colObj0);
	btRigidBody* body1=btRigidBody::upcast(colObj1);

	btSolverConstraint& solverConstraint = m_tmpSolverContactFrictionConstraintPool.expand();
	memset(&solverConstraint,0xff,sizeof(btSolverConstraint));
	solverConstraint.m_contactNormal = normalAxis;

	solverConstraint.m_solverBodyIdA = solverBodyIdA;
	solverConstraint.m_solverBodyIdB = solverBodyIdB;
	solverConstraint.m_frictionIndex = frictionIndex;

	solverConstraint.m_friction = cp.m_combinedFriction;
	solverConstraint.m_originalContactPoint = 0;

	solverConstraint.m_appliedImpulse = 0.f;
	solverConstraint.m_appliedPushImpulse = 0.f;

	{
		btVector3 ftorqueAxis1 = rel_pos1.cross(solverConstraint.m_contactNormal);
		solverConstraint.m_relpos1CrossNormal = ftorqueAxis1;
#if defined(_MSC_VER) && defined(_M_IX86)
		// MMD 0x4FCA40: rows fold (z+y)+x, same as the contact path.
		solverConstraint.m_angularComponentA = body0 ? mmdSolverAngularComponent210(body0->getInvInertiaTensorWorld(), ftorqueAxis1, body0->getAngularFactor()) : btVector3(0,0,0);
#else
		solverConstraint.m_angularComponentA = body0 ? body0->getInvInertiaTensorWorld()*ftorqueAxis1*body0->getAngularFactor() : btVector3(0,0,0);
#endif
	}
	{
		btVector3 ftorqueAxis1 = rel_pos2.cross(-solverConstraint.m_contactNormal);
		solverConstraint.m_relpos2CrossNormal = ftorqueAxis1;
#if defined(_MSC_VER) && defined(_M_IX86)
		solverConstraint.m_angularComponentB = body1 ? mmdSolverAngularComponent210(body1->getInvInertiaTensorWorld(), ftorqueAxis1, body1->getAngularFactor()) : btVector3(0,0,0);
#else
		solverConstraint.m_angularComponentB = body1 ? body1->getInvInertiaTensorWorld()*ftorqueAxis1*body1->getAngularFactor() : btVector3(0,0,0);
#endif
	}

#ifdef COMPUTE_IMPULSE_DENOM
	btScalar denom0 = rb0->computeImpulseDenominator(pos1,solverConstraint.m_contactNormal);
	btScalar denom1 = rb1->computeImpulseDenominator(pos2,solverConstraint.m_contactNormal);
#elif defined(_MSC_VER) && defined(_M_IX86)
	// MMD 0x4FCA40 (VC9): same grouping as the contact path - the dot
	// sums as ((n.z*cz + n.y*cy) + n.x*cx) and invMass joins LAST.
	btScalar denom0 = 0.f;
	btScalar denom1 = 0.f;
	if (body0)
	{
		const btVector3& a = solverConstraint.m_angularComponentA;
		const btScalar crossX = mmdSolverSub(mmdSolverMul(a.getY(), rel_pos1.getZ()), mmdSolverMul(a.getZ(), rel_pos1.getY()));
		const btScalar crossY = mmdSolverSub(mmdSolverMul(a.getZ(), rel_pos1.getX()), mmdSolverMul(a.getX(), rel_pos1.getZ()));
		const btScalar crossZ = mmdSolverSub(mmdSolverMul(a.getX(), rel_pos1.getY()), mmdSolverMul(a.getY(), rel_pos1.getX()));
		denom0 = mmdSolverAdd(mmdSolverDot210(normalAxis.getX(), crossX, normalAxis.getY(), crossY, normalAxis.getZ(), crossZ), body0->getInvMass());
	}
	if (body1)
	{
		const btVector3& b2c = solverConstraint.m_angularComponentB;
		const btVector3 a(mmdSolverSub(btScalar(0), b2c.getX()), mmdSolverSub(btScalar(0), b2c.getY()), mmdSolverSub(btScalar(0), b2c.getZ()));
		const btScalar crossX = mmdSolverSub(mmdSolverMul(a.getY(), rel_pos2.getZ()), mmdSolverMul(a.getZ(), rel_pos2.getY()));
		const btScalar crossY = mmdSolverSub(mmdSolverMul(a.getZ(), rel_pos2.getX()), mmdSolverMul(a.getX(), rel_pos2.getZ()));
		const btScalar crossZ = mmdSolverSub(mmdSolverMul(a.getX(), rel_pos2.getY()), mmdSolverMul(a.getY(), rel_pos2.getX()));
		denom1 = mmdSolverAdd(mmdSolverDot210(normalAxis.getX(), crossX, normalAxis.getY(), crossY, normalAxis.getZ(), crossZ), body1->getInvMass());
	}
#else
	btVector3 vec;
	btScalar denom0 = 0.f;
	btScalar denom1 = 0.f;
	if (body0)
	{
		vec = ( solverConstraint.m_angularComponentA).cross(rel_pos1);
		denom0 = body0->getInvMass() + normalAxis.dot(vec);
	}
	if (body1)
	{
		vec = ( -solverConstraint.m_angularComponentB).cross(rel_pos2);
		denom1 = body1->getInvMass() + normalAxis.dot(vec);
	}


#endif //COMPUTE_IMPULSE_DENOM
	btScalar denom = relaxation/(denom0+denom1);
	solverConstraint.m_jacDiagABInv = denom;

#ifdef _USE_JACOBIAN
	solverConstraint.m_jac =  btJacobianEntry (
		rel_pos1,rel_pos2,solverConstraint.m_contactNormal,
		body0->getInvInertiaDiagLocal(),
		body0->getInvMass(),
		body1->getInvInertiaDiagLocal(),
		body1->getInvMass());
#endif //_USE_JACOBIAN


	{
		btScalar rel_vel;
#if defined(_MSC_VER) && defined(_M_IX86)
		// MMD 0x4FCA40: same interleaved chains as the contact path.
		btScalar vel1Dotn;
		btScalar vel2Dotn;
		{
			const btVector3& l0 = body0 ? body0->getLinearVelocity() : btVector3(0,0,0);
			const btVector3& a0 = body0 ? body0->getAngularVelocity() : btVector3(0,0,0);
			const btVector3& l1 = body1 ? body1->getLinearVelocity() : btVector3(0,0,0);
			const btVector3& a1 = body1 ? body1->getAngularVelocity() : btVector3(0,0,0);
			const btVector3& nv = solverConstraint.m_contactNormal;
			const btVector3& r1 = solverConstraint.m_relpos1CrossNormal;
			const btVector3& r2 = solverConstraint.m_relpos2CrossNormal;
			// MMD 0x4FCA40: the FRICTION path folds the linear terms first -
			// n.z*l.z + n.y*l.y + a.z*r.z + a.y*r.y + r.x*a.x + n.x*l.x -
			// unlike the contact path's angular-first interleave.
			const btScalar v1a = mmdSolverAdd(mmdSolverMul(nv.getZ(), l0.getZ()), mmdSolverMul(nv.getY(), l0.getY()));
			const btScalar v1b = mmdSolverAdd(v1a, mmdSolverMul(a0.getZ(), r1.getZ()));
			const btScalar v1c = mmdSolverAdd(v1b, mmdSolverMul(a0.getY(), r1.getY()));
			const btScalar v1d = mmdSolverAdd(v1c, mmdSolverMul(r1.getX(), a0.getX()));
			vel1Dotn = mmdSolverAdd(v1d, mmdSolverMul(nv.getX(), l0.getX()));
			const btScalar v2a = mmdSolverSub(mmdSolverMul(r2.getZ(), a1.getZ()), mmdSolverMul(nv.getZ(), l1.getZ()));
			const btScalar v2b = mmdSolverSub(v2a, mmdSolverMul(nv.getY(), l1.getY()));
			const btScalar v2c = mmdSolverAdd(v2b, mmdSolverMul(r2.getY(), a1.getY()));
			const btScalar v2d = mmdSolverSub(v2c, mmdSolverMul(nv.getX(), l1.getX()));
			vel2Dotn = mmdSolverAdd(v2d, mmdSolverMul(a1.getX(), r2.getX()));
		}
#else
		btScalar vel1Dotn = solverConstraint.m_contactNormal.dot(body0?body0->getLinearVelocity():btVector3(0,0,0))
			+ solverConstraint.m_relpos1CrossNormal.dot(body0?body0->getAngularVelocity():btVector3(0,0,0));
		btScalar vel2Dotn = -solverConstraint.m_contactNormal.dot(body1?body1->getLinearVelocity():btVector3(0,0,0))
			+ solverConstraint.m_relpos2CrossNormal.dot(body1?body1->getAngularVelocity():btVector3(0,0,0));
#endif

		rel_vel = vel1Dotn+vel2Dotn;

//		btScalar positionalError = 0.f;

		btSimdScalar velocityError =  - rel_vel;
		btSimdScalar	velocityImpulse = velocityError * btSimdScalar(solverConstraint.m_jacDiagABInv);
		solverConstraint.m_rhs = velocityImpulse;
		solverConstraint.m_cfm = 0.f;
		solverConstraint.m_lowerLimit = 0;
		solverConstraint.m_upperLimit = 1e10f;
	}

	return solverConstraint;
}

int	btSequentialImpulseConstraintSolver::getOrInitSolverBody(btCollisionObject& body)
{
	int solverBodyIdA = -1;

	if (body.getCompanionId() >= 0)
	{
		//body has already been converted
		solverBodyIdA = body.getCompanionId();
	} else
	{
		btRigidBody* rb = btRigidBody::upcast(&body);
		if (rb && rb->getInvMass())
		{
			solverBodyIdA = m_tmpSolverBodyPool.size();
			btSolverBody& solverBody = m_tmpSolverBodyPool.expand();
			initSolverBody(&solverBody,&body);
			body.setCompanionId(solverBodyIdA);
		} else
		{
			return 0;//assume first one is a fixed solver body
		}
	}
	return solverBodyIdA;
}
#include <stdio.h>



void	btSequentialImpulseConstraintSolver::convertContact(btPersistentManifold* manifold,const btContactSolverInfo& infoGlobal)
{
	btCollisionObject* colObj0=0,*colObj1=0;

	colObj0 = (btCollisionObject*)manifold->getBody0();
	colObj1 = (btCollisionObject*)manifold->getBody1();

	int solverBodyIdA=-1;
	int solverBodyIdB=-1;

	if (manifold->getNumContacts())
	{
		solverBodyIdA = getOrInitSolverBody(*colObj0);
		solverBodyIdB = getOrInitSolverBody(*colObj1);
	}

	///avoid collision response between two static objects
	if (!solverBodyIdA && !solverBodyIdB)
		return;

	btVector3 rel_pos1;
	btVector3 rel_pos2;
	btScalar relaxation;

	for (int j=0;j<manifold->getNumContacts();j++)
	{

		btManifoldPoint& cp = manifold->getContactPoint(j);

		if (cp.getDistance() <= manifold->getContactProcessingThreshold())
		{

			const btVector3& pos1 = cp.getPositionWorldOnA();
			const btVector3& pos2 = cp.getPositionWorldOnB();

			rel_pos1 = pos1 - colObj0->getWorldTransform().getOrigin(); 
			rel_pos2 = pos2 - colObj1->getWorldTransform().getOrigin();


			relaxation = 1.f;
			btScalar rel_vel;
			btVector3 vel;

			int frictionIndex = m_tmpSolverContactConstraintPool.size();

			{
				btSolverConstraint& solverConstraint = m_tmpSolverContactConstraintPool.expand();
				btRigidBody* rb0 = btRigidBody::upcast(colObj0);
				btRigidBody* rb1 = btRigidBody::upcast(colObj1);

				solverConstraint.m_solverBodyIdA = solverBodyIdA;
				solverConstraint.m_solverBodyIdB = solverBodyIdB;

				solverConstraint.m_originalContactPoint = &cp;

				btVector3 torqueAxis0 = rel_pos1.cross(cp.m_normalWorldOnB);
#if defined(_MSC_VER) && defined(_M_IX86)
				// MMD 0x4FD380 @0x4FD950: rows fold (z+y)+x, see helper.
				solverConstraint.m_angularComponentA = rb0 ? mmdSolverAngularComponent210(rb0->getInvInertiaTensorWorld(), torqueAxis0, rb0->getAngularFactor()) : btVector3(0,0,0);
#else
				solverConstraint.m_angularComponentA = rb0 ? rb0->getInvInertiaTensorWorld()*torqueAxis0*rb0->getAngularFactor() : btVector3(0,0,0);
#endif
				btVector3 torqueAxis1 = rel_pos2.cross(cp.m_normalWorldOnB);		
#if defined(_MSC_VER) && defined(_M_IX86)
				solverConstraint.m_angularComponentB = rb1 ? mmdSolverAngularComponent210(rb1->getInvInertiaTensorWorld(), btVector3(-torqueAxis1.getX(), -torqueAxis1.getY(), -torqueAxis1.getZ()), rb1->getAngularFactor()) : btVector3(0,0,0);
#else
				solverConstraint.m_angularComponentB = rb1 ? rb1->getInvInertiaTensorWorld()*-torqueAxis1*rb1->getAngularFactor() : btVector3(0,0,0);
#endif
				{
#ifdef COMPUTE_IMPULSE_DENOM
					btScalar denom0 = rb0->computeImpulseDenominator(pos1,cp.m_normalWorldOnB);
					btScalar denom1 = rb1->computeImpulseDenominator(pos2,cp.m_normalWorldOnB);
#else							
					btVector3 vec;
					btScalar denom0 = 0.f;
					btScalar denom1 = 0.f;
#if defined(_MSC_VER) && defined(_M_IX86)
					// MMD 0x4FD380 (VC9): n.(angComp x rel) sums as
					// ((n.z*cz + n.y*cy) + n.x*cx) and the inverse mass joins LAST.
					if (rb0)
					{
						const btVector3& a = solverConstraint.m_angularComponentA;
						const btScalar crossX = mmdSolverSub(mmdSolverMul(a.getY(), rel_pos1.getZ()), mmdSolverMul(a.getZ(), rel_pos1.getY()));
						const btScalar crossY = mmdSolverSub(mmdSolverMul(a.getZ(), rel_pos1.getX()), mmdSolverMul(a.getX(), rel_pos1.getZ()));
						const btScalar crossZ = mmdSolverSub(mmdSolverMul(a.getX(), rel_pos1.getY()), mmdSolverMul(a.getY(), rel_pos1.getX()));
						denom0 = mmdSolverAdd(mmdSolverDot210(cp.m_normalWorldOnB.getX(), crossX, cp.m_normalWorldOnB.getY(), crossY, cp.m_normalWorldOnB.getZ(), crossZ), rb0->getInvMass());
					}
					if (rb1)
					{
						const btVector3& b2c = solverConstraint.m_angularComponentB;
						const btVector3 a(mmdSolverSub(btScalar(0), b2c.getX()), mmdSolverSub(btScalar(0), b2c.getY()), mmdSolverSub(btScalar(0), b2c.getZ()));
						const btScalar crossX = mmdSolverSub(mmdSolverMul(a.getY(), rel_pos2.getZ()), mmdSolverMul(a.getZ(), rel_pos2.getY()));
						const btScalar crossY = mmdSolverSub(mmdSolverMul(a.getZ(), rel_pos2.getX()), mmdSolverMul(a.getX(), rel_pos2.getZ()));
						const btScalar crossZ = mmdSolverSub(mmdSolverMul(a.getX(), rel_pos2.getY()), mmdSolverMul(a.getY(), rel_pos2.getX()));
						denom1 = mmdSolverAdd(mmdSolverDot210(cp.m_normalWorldOnB.getX(), crossX, cp.m_normalWorldOnB.getY(), crossY, cp.m_normalWorldOnB.getZ(), crossZ), rb1->getInvMass());
					}
#else
					if (rb0)
					{
						vec = ( solverConstraint.m_angularComponentA).cross(rel_pos1);
						denom0 = rb0->getInvMass() + cp.m_normalWorldOnB.dot(vec);
					}
					if (rb1)
					{
						vec = ( -solverConstraint.m_angularComponentB).cross(rel_pos2);
						denom1 = rb1->getInvMass() + cp.m_normalWorldOnB.dot(vec);
					}
#endif
#endif //COMPUTE_IMPULSE_DENOM		

					btScalar denom = relaxation/(denom0+denom1);
					solverConstraint.m_jacDiagABInv = denom;
				}

				solverConstraint.m_contactNormal = cp.m_normalWorldOnB;
				solverConstraint.m_relpos1CrossNormal = rel_pos1.cross(cp.m_normalWorldOnB);
				solverConstraint.m_relpos2CrossNormal = rel_pos2.cross(-cp.m_normalWorldOnB);


				btVector3 vel1 = rb0 ? rb0->getVelocityInLocalPoint(rel_pos1) : btVector3(0,0,0);
				btVector3 vel2 = rb1 ? rb1->getVelocityInLocalPoint(rel_pos2) : btVector3(0,0,0);

				vel  = vel1 - vel2;

#if defined(_MSC_VER) && defined(_M_IX86)
				// MMD 0x4FD380: (n.z*v.z + n.y*v.y) + n.x*v.x.
				rel_vel = mmdSolverDot210(cp.m_normalWorldOnB.getX(), vel.getX(), cp.m_normalWorldOnB.getY(), vel.getY(), cp.m_normalWorldOnB.getZ(), vel.getZ());
#else
				rel_vel = cp.m_normalWorldOnB.dot(vel);
#endif

				btScalar penetration = cp.getDistance()+infoGlobal.m_linearSlop;


				solverConstraint.m_friction = cp.m_combinedFriction;

				btScalar restitution = 0.f;
				
				if (cp.m_lifeTime>infoGlobal.m_restingContactRestitutionThreshold)
				{
					restitution = 0.f;
				} else
				{
					restitution =  restitutionCurve(rel_vel, cp.m_combinedRestitution);
					if (restitution <= btScalar(0.))
					{
						restitution = 0.f;
					};
				}


				///warm starting (or zero if disabled)
				if (infoGlobal.m_solverMode & SOLVER_USE_WARMSTARTING)
				{
					solverConstraint.m_appliedImpulse = cp.m_appliedImpulse * infoGlobal.m_warmstartingFactor;
					if (rb0)
						m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdA].applyImpulse(solverConstraint.m_contactNormal*rb0->getInvMass()*rb0->getLinearFactor(),solverConstraint.m_angularComponentA,solverConstraint.m_appliedImpulse);
					if (rb1)
						m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdB].applyImpulse(solverConstraint.m_contactNormal*rb1->getInvMass()*rb1->getLinearFactor(),-solverConstraint.m_angularComponentB,-solverConstraint.m_appliedImpulse);
				} else
				{
					solverConstraint.m_appliedImpulse = 0.f;
				}

				solverConstraint.m_appliedPushImpulse = 0.f;

				{

					btScalar rel_vel;
					btScalar vel1Dotn;
					btScalar vel2Dotn;
#if defined(_MSC_VER) && defined(_M_IX86)
					// MMD 0x4FD380 (VC9): the velocity dots sum as (z+y)+x
					// (same lane order as the SIMD resolve dots).
					{
						const btVector3& l0 = rb0 ? rb0->getLinearVelocity() : btVector3(0,0,0);
						const btVector3& a0 = rb0 ? rb0->getAngularVelocity() : btVector3(0,0,0);
						const btVector3& l1 = rb1 ? rb1->getLinearVelocity() : btVector3(0,0,0);
						const btVector3& a1 = rb1 ? rb1->getAngularVelocity() : btVector3(0,0,0);
						const btVector3& nv = solverConstraint.m_contactNormal;
						const btVector3& r1 = solverConstraint.m_relpos1CrossNormal;
						const btVector3& r2 = solverConstraint.m_relpos2CrossNormal;
						// MMD 0x4FD380 @0x4FE3E8/0x4FE4B6 (VC9): interleaved
						// left-assoc chains exactly as the instructions sum
						// them: v1 = a.z*r.z + a.y*r.y + n.z*l.z + n.y*l.y +
						// a.x*r.x + l.x*n.x;  v2 = r.z*a.z - n.z*l.z -
						// n.y*l.y + r.y*a.y - n.x*l.x + a.x*r.x.
						const btScalar v1a = mmdSolverAdd(mmdSolverMul(a0.getZ(), r1.getZ()), mmdSolverMul(a0.getY(), r1.getY()));
						const btScalar v1b = mmdSolverAdd(v1a, mmdSolverMul(nv.getZ(), l0.getZ()));
						const btScalar v1c = mmdSolverAdd(v1b, mmdSolverMul(nv.getY(), l0.getY()));
						const btScalar v1d = mmdSolverAdd(v1c, mmdSolverMul(a0.getX(), r1.getX()));
						vel1Dotn = mmdSolverAdd(v1d, mmdSolverMul(l0.getX(), nv.getX()));
						const btScalar v2a = mmdSolverSub(mmdSolverMul(r2.getZ(), a1.getZ()), mmdSolverMul(nv.getZ(), l1.getZ()));
						const btScalar v2b = mmdSolverSub(v2a, mmdSolverMul(nv.getY(), l1.getY()));
						const btScalar v2c = mmdSolverAdd(v2b, mmdSolverMul(r2.getY(), a1.getY()));
						const btScalar v2d = mmdSolverSub(v2c, mmdSolverMul(nv.getX(), l1.getX()));
						vel2Dotn = mmdSolverAdd(v2d, mmdSolverMul(a1.getX(), r2.getX()));
					}
#else
					vel1Dotn = solverConstraint.m_contactNormal.dot(rb0?rb0->getLinearVelocity():btVector3(0,0,0))
						+ solverConstraint.m_relpos1CrossNormal.dot(rb0?rb0->getAngularVelocity():btVector3(0,0,0));
					vel2Dotn = -solverConstraint.m_contactNormal.dot(rb1?rb1->getLinearVelocity():btVector3(0,0,0))
						+ solverConstraint.m_relpos2CrossNormal.dot(rb1?rb1->getAngularVelocity():btVector3(0,0,0));
#endif
					rel_vel = vel1Dotn+vel2Dotn;
					btScalar positionalError = 0.f;
				#if defined(_MSC_VER) && defined(_M_IX86)
					// MMD 0x4FD380 @0x4FE4B6 (VC9): ((-1/dt)*erp)*penetration,
					// a different (and for some penetrations 1-ULP-different)
					// tree than (-pen)*erp/dt.
					positionalError = mmdSolverMul(mmdSolverMul((btScalar)((-1.0) / (double)infoGlobal.m_timeStep), infoGlobal.m_erp), penetration);
				#else
					positionalError = -penetration * infoGlobal.m_erp/infoGlobal.m_timeStep;
				#endif
					btScalar	velocityError = restitution - rel_vel;// * damping;
					btScalar  penetrationImpulse = positionalError*solverConstraint.m_jacDiagABInv;
					btScalar velocityImpulse = velocityError *solverConstraint.m_jacDiagABInv;
					if (g_openmmdSolveCount <= openmmdSolveLimit() && openmmdLogOpen()) {
						fprintf(g_openmmdLog,
							"{\"n\":%d,\"kind\":\"conv2\",\"vel1\":\"%08X\",\"vel2\":\"%08X\","
							"\"relvel\":\"%08X\",\"posErr\":\"%08X\",\"velErr\":\"%08X\","
							"\"rest2\":\"%08X\",\"pen\":\"%08X\",\"pi\":\"%08X\",\"vi\":\"%08X\"}\n",
							++g_openmmdSeq, openmmdBits(vel1Dotn), openmmdBits(vel2Dotn),
							openmmdBits(rel_vel), openmmdBits(positionalError),
							openmmdBits(velocityError), openmmdBits(restitution),
							openmmdBits(penetration), openmmdBits(penetrationImpulse),
							openmmdBits(velocityImpulse));
					}
					if (!infoGlobal.m_splitImpulse || (penetration > infoGlobal.m_splitImpulsePenetrationThreshold))
					{
						//combine position and velocity into rhs
						solverConstraint.m_rhs = penetrationImpulse+velocityImpulse;
						solverConstraint.m_rhsPenetration = 0.f;
					} else
					{
						//split position and velocity into rhs and m_rhsPenetration
						solverConstraint.m_rhs = velocityImpulse;
						solverConstraint.m_rhsPenetration = penetrationImpulse;
					}
					solverConstraint.m_cfm = 0.f;
					solverConstraint.m_lowerLimit = 0;
					solverConstraint.m_upperLimit = 1e10f;
				}
				openmmdLogConvert(manifold, j, cp, rb0, rb1, rel_pos1, rel_pos2,
				                  solverConstraint.m_angularComponentA,
				                  solverConstraint.m_angularComponentB,
				                  infoGlobal, solverConstraint);


				/////setup the friction constraints



				if (1)
				{
					solverConstraint.m_frictionIndex = m_tmpSolverContactFrictionConstraintPool.size();
					if (!(infoGlobal.m_solverMode & SOLVER_ENABLE_FRICTION_DIRECTION_CACHING) || !cp.m_lateralFrictionInitialized)
					{
						cp.m_lateralFrictionDir1 = vel - cp.m_normalWorldOnB * rel_vel;
						btScalar lat_rel_vel = cp.m_lateralFrictionDir1.length2();
						if (!(infoGlobal.m_solverMode & SOLVER_DISABLE_VELOCITY_DEPENDENT_FRICTION_DIRECTION) && lat_rel_vel > SIMD_EPSILON)
						{
#if defined(_MSC_VER) && defined(_M_IX86)
							// MMD 0x4FD380 (VC9): the direction scales by the
							// x87 EXTENDED reciprocal 1/sqrt(L2), each
							// component rounded once (not v /= sqrt(L2)).
							{
								const double mmdInv = 1.0 / sqrt((double)lat_rel_vel);
								cp.m_lateralFrictionDir1.setValue(
									(btScalar)((double)cp.m_lateralFrictionDir1.getX() * mmdInv),
									(btScalar)((double)cp.m_lateralFrictionDir1.getY() * mmdInv),
									(btScalar)((double)cp.m_lateralFrictionDir1.getZ() * mmdInv));
							}
#else
							cp.m_lateralFrictionDir1 /= btSqrt(lat_rel_vel);
#endif
							if((infoGlobal.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS))
							{
								cp.m_lateralFrictionDir2 = cp.m_lateralFrictionDir1.cross(cp.m_normalWorldOnB);
								cp.m_lateralFrictionDir2.normalize();//??
								applyAnisotropicFriction(colObj0,cp.m_lateralFrictionDir2);
								applyAnisotropicFriction(colObj1,cp.m_lateralFrictionDir2);
								addFrictionConstraint(cp.m_lateralFrictionDir2,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);
							}

							applyAnisotropicFriction(colObj0,cp.m_lateralFrictionDir1);
							applyAnisotropicFriction(colObj1,cp.m_lateralFrictionDir1);
							addFrictionConstraint(cp.m_lateralFrictionDir1,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);
							cp.m_lateralFrictionInitialized = true;
						} else
						{
							//re-calculate friction direction every frame, todo: check if this is really needed
							btPlaneSpace1(cp.m_normalWorldOnB,cp.m_lateralFrictionDir1,cp.m_lateralFrictionDir2);
							if ((infoGlobal.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS))
							{
								applyAnisotropicFriction(colObj0,cp.m_lateralFrictionDir2);
								applyAnisotropicFriction(colObj1,cp.m_lateralFrictionDir2);
								addFrictionConstraint(cp.m_lateralFrictionDir2,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);
							}

							applyAnisotropicFriction(colObj0,cp.m_lateralFrictionDir1);
							applyAnisotropicFriction(colObj1,cp.m_lateralFrictionDir1);
							addFrictionConstraint(cp.m_lateralFrictionDir1,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);

							cp.m_lateralFrictionInitialized = true;
						}

					} else
					{
						addFrictionConstraint(cp.m_lateralFrictionDir1,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);
						if ((infoGlobal.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS))
							addFrictionConstraint(cp.m_lateralFrictionDir2,solverBodyIdA,solverBodyIdB,frictionIndex,cp,rel_pos1,rel_pos2,colObj0,colObj1, relaxation);
					}

					if (infoGlobal.m_solverMode & SOLVER_USE_FRICTION_WARMSTARTING)
					{
						{
							btSolverConstraint& frictionConstraint1 = m_tmpSolverContactFrictionConstraintPool[solverConstraint.m_frictionIndex];
							if (infoGlobal.m_solverMode & SOLVER_USE_WARMSTARTING)
							{
								frictionConstraint1.m_appliedImpulse = cp.m_appliedImpulseLateral1 * infoGlobal.m_warmstartingFactor;
								if (rb0)
									m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdA].applyImpulse(frictionConstraint1.m_contactNormal*rb0->getInvMass()*rb0->getLinearFactor(),frictionConstraint1.m_angularComponentA,frictionConstraint1.m_appliedImpulse);
								if (rb1)
									m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdB].applyImpulse(frictionConstraint1.m_contactNormal*rb1->getInvMass()*rb1->getLinearFactor(),-frictionConstraint1.m_angularComponentB,-frictionConstraint1.m_appliedImpulse);
							} else
							{
								frictionConstraint1.m_appliedImpulse = 0.f;
							}
						}

						if ((infoGlobal.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS))
						{
							btSolverConstraint& frictionConstraint2 = m_tmpSolverContactFrictionConstraintPool[solverConstraint.m_frictionIndex+1];
							if (infoGlobal.m_solverMode & SOLVER_USE_WARMSTARTING)
							{
								frictionConstraint2.m_appliedImpulse = cp.m_appliedImpulseLateral2 * infoGlobal.m_warmstartingFactor;
								if (rb0)
									m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdA].applyImpulse(frictionConstraint2.m_contactNormal*rb0->getInvMass(),frictionConstraint2.m_angularComponentA,frictionConstraint2.m_appliedImpulse);
								if (rb1)
									m_tmpSolverBodyPool[solverConstraint.m_solverBodyIdB].applyImpulse(frictionConstraint2.m_contactNormal*rb1->getInvMass(),-frictionConstraint2.m_angularComponentB,-frictionConstraint2.m_appliedImpulse);
							} else
							{
								frictionConstraint2.m_appliedImpulse = 0.f;
							}
						}
					} else
					{
						btSolverConstraint& frictionConstraint1 = m_tmpSolverContactFrictionConstraintPool[solverConstraint.m_frictionIndex];
						frictionConstraint1.m_appliedImpulse = 0.f;
						if ((infoGlobal.m_solverMode & SOLVER_USE_2_FRICTION_DIRECTIONS))
						{
							btSolverConstraint& frictionConstraint2 = m_tmpSolverContactFrictionConstraintPool[solverConstraint.m_frictionIndex+1];
							frictionConstraint2.m_appliedImpulse = 0.f;
						}
					}
				}
			}


		}
	}
}


btScalar btSequentialImpulseConstraintSolver::solveGroupCacheFriendlySetup(btCollisionObject** /*bodies */,int /*numBodies */,btPersistentManifold** manifoldPtr, int numManifolds,btTypedConstraint** constraints,int numConstraints,const btContactSolverInfo& infoGlobal,btIDebugDraw* debugDrawer,btStackAlloc* stackAlloc)
{
	BT_PROFILE("solveGroupCacheFriendlySetup");
	(void)stackAlloc;
	(void)debugDrawer;


	if (!(numConstraints + numManifolds))
	{
		//		printf("empty\n");
		return 0.f;
	}

	if (1)
	{
		int j;
		for (j=0;j<numConstraints;j++)
		{
			btTypedConstraint* constraint = constraints[j];
			constraint->buildJacobian();
		}
	}

	btSolverBody& fixedBody = m_tmpSolverBodyPool.expand();
	initSolverBody(&fixedBody,0);

	//btRigidBody* rb0=0,*rb1=0;

	//if (1)
	{
		{

			int totalNumRows = 0;
			int i;
			
			m_tmpConstraintSizesPool.resize(numConstraints);
			//calculate the total number of contraint rows
			for (i=0;i<numConstraints;i++)
			{
				btTypedConstraint::btConstraintInfo1& info1 = m_tmpConstraintSizesPool[i];
				constraints[i]->getInfo1(&info1);
				totalNumRows += info1.m_numConstraintRows;
			}
			m_tmpSolverNonContactConstraintPool.resize(totalNumRows);

			
			///setup the btSolverConstraints
			int currentRow = 0;

			for (i=0;i<numConstraints;i++)
			{
				const btTypedConstraint::btConstraintInfo1& info1 = m_tmpConstraintSizesPool[i];
				
				if (info1.m_numConstraintRows)
				{
					btAssert(currentRow<totalNumRows);

					btSolverConstraint* currentConstraintRow = &m_tmpSolverNonContactConstraintPool[currentRow];
					btTypedConstraint* constraint = constraints[i];



					btRigidBody& rbA = constraint->getRigidBodyA();
					btRigidBody& rbB = constraint->getRigidBodyB();

					int solverBodyIdA = getOrInitSolverBody(rbA);
					int solverBodyIdB = getOrInitSolverBody(rbB);

					btSolverBody* bodyAPtr = &m_tmpSolverBodyPool[solverBodyIdA];
					btSolverBody* bodyBPtr = &m_tmpSolverBodyPool[solverBodyIdB];

					int j;
					for ( j=0;j<info1.m_numConstraintRows;j++)
					{
						memset(&currentConstraintRow[j],0,sizeof(btSolverConstraint));
						currentConstraintRow[j].m_lowerLimit = -FLT_MAX;
						currentConstraintRow[j].m_upperLimit = FLT_MAX;
						currentConstraintRow[j].m_appliedImpulse = 0.f;
						currentConstraintRow[j].m_appliedPushImpulse = 0.f;
						currentConstraintRow[j].m_solverBodyIdA = solverBodyIdA;
						currentConstraintRow[j].m_solverBodyIdB = solverBodyIdB;
					}

					bodyAPtr->m_deltaLinearVelocity.setValue(0.f,0.f,0.f);
					bodyAPtr->m_deltaAngularVelocity.setValue(0.f,0.f,0.f);
					bodyBPtr->m_deltaLinearVelocity.setValue(0.f,0.f,0.f);
					bodyBPtr->m_deltaAngularVelocity.setValue(0.f,0.f,0.f);



					btTypedConstraint::btConstraintInfo2 info2;
					info2.fps = 1.f/infoGlobal.m_timeStep;
					info2.erp = infoGlobal.m_erp;
					info2.m_J1linearAxis = currentConstraintRow->m_contactNormal;
					info2.m_J1angularAxis = currentConstraintRow->m_relpos1CrossNormal;
					info2.m_J2linearAxis = 0;
					info2.m_J2angularAxis = currentConstraintRow->m_relpos2CrossNormal;
					info2.rowskip = sizeof(btSolverConstraint)/sizeof(btScalar);//check this
					///the size of btSolverConstraint needs be a multiple of btScalar
					btAssert(info2.rowskip*sizeof(btScalar)== sizeof(btSolverConstraint));
					info2.m_constraintError = &currentConstraintRow->m_rhs;
					info2.cfm = &currentConstraintRow->m_cfm;
					info2.m_lowerLimit = &currentConstraintRow->m_lowerLimit;
					info2.m_upperLimit = &currentConstraintRow->m_upperLimit;
					info2.m_numIterations = infoGlobal.m_numIterations;
					constraints[i]->getInfo2(&info2);

					///finalize the constraint setup
					for ( j=0;j<info1.m_numConstraintRows;j++)
					{
						btSolverConstraint& solverConstraint = currentConstraintRow[j];

						{
							const btVector3& ftorqueAxis1 = solverConstraint.m_relpos1CrossNormal;
#if defined(_MSC_VER) && defined(_M_IX86)
							solverConstraint.m_angularComponentA = mmdSolverAngularComponentVc9(
								constraint->getRigidBodyA().getInvInertiaTensorWorld(),
								ftorqueAxis1, constraint->getRigidBodyA().getAngularFactor());
#else
							solverConstraint.m_angularComponentA = constraint->getRigidBodyA().getInvInertiaTensorWorld()*ftorqueAxis1*constraint->getRigidBodyA().getAngularFactor();
#endif
						}
						{
							const btVector3& ftorqueAxis2 = solverConstraint.m_relpos2CrossNormal;
#if defined(_MSC_VER) && defined(_M_IX86)
							solverConstraint.m_angularComponentB = mmdSolverAngularComponentVc9(
								constraint->getRigidBodyB().getInvInertiaTensorWorld(),
								ftorqueAxis2, constraint->getRigidBodyB().getAngularFactor());
#else
							solverConstraint.m_angularComponentB = constraint->getRigidBodyB().getInvInertiaTensorWorld()*ftorqueAxis2*constraint->getRigidBodyB().getAngularFactor();
#endif
						}

						{
#if defined(_MSC_VER) && defined(_M_IX86)
							solverConstraint.m_jacDiagABInv = mmdSolverJacDiagVc9(
								rbA.getInvInertiaTensorWorld(), rbA.getInvMass(),
								rbB.getInvInertiaTensorWorld(), rbB.getInvMass(),
								solverConstraint.m_contactNormal,
								solverConstraint.m_relpos1CrossNormal,
								solverConstraint.m_relpos2CrossNormal);
#else
							btVector3 iMJlA = solverConstraint.m_contactNormal*rbA.getInvMass();
							btVector3 iMJaA = rbA.getInvInertiaTensorWorld()*solverConstraint.m_relpos1CrossNormal;
							btVector3 iMJlB = solverConstraint.m_contactNormal*rbB.getInvMass();//sign of normal?
							btVector3 iMJaB = rbB.getInvInertiaTensorWorld()*solverConstraint.m_relpos2CrossNormal;

							btScalar sum = iMJlA.dot(solverConstraint.m_contactNormal);
							sum += iMJaA.dot(solverConstraint.m_relpos1CrossNormal);
							sum += iMJlB.dot(solverConstraint.m_contactNormal);
							sum += iMJaB.dot(solverConstraint.m_relpos2CrossNormal);

							solverConstraint.m_jacDiagABInv = btScalar(1.)/sum;
#endif
						}


						///fix rhs
						///todo: add force/torque accelerators
						{
#if defined(_MSC_VER) && defined(_M_IX86)
							solverConstraint.m_rhs = mmdSolverRhsVc9(
								rbA, rbB, solverConstraint.m_contactNormal,
								solverConstraint.m_relpos1CrossNormal,
								solverConstraint.m_relpos2CrossNormal,
								solverConstraint.m_jacDiagABInv,
								solverConstraint.m_rhs);
							solverConstraint.m_appliedImpulse = 0.f;
#else
							btScalar rel_vel;
							btScalar vel1Dotn = solverConstraint.m_contactNormal.dot(rbA.getLinearVelocity()) + solverConstraint.m_relpos1CrossNormal.dot(rbA.getAngularVelocity());
							btScalar vel2Dotn = -solverConstraint.m_contactNormal.dot(rbB.getLinearVelocity()) + solverConstraint.m_relpos2CrossNormal.dot(rbB.getAngularVelocity());

							rel_vel = vel1Dotn+vel2Dotn;

							btScalar restitution = 0.f;
							btScalar positionalError = solverConstraint.m_rhs;//already filled in by getConstraintInfo2
							btScalar	velocityError = restitution - rel_vel;// * damping;
							btScalar	penetrationImpulse = positionalError*solverConstraint.m_jacDiagABInv;
							btScalar	velocityImpulse = velocityError *solverConstraint.m_jacDiagABInv;
							solverConstraint.m_rhs = penetrationImpulse+velocityImpulse;
							solverConstraint.m_appliedImpulse = 0.f;
#endif
						}
					}
				}
				currentRow+=m_tmpConstraintSizesPool[i].m_numConstraintRows;
			}
		}

		{
			int i;
			btPersistentManifold* manifold = 0;
//			btCollisionObject* colObj0=0,*colObj1=0;


			for (i=0;i<numManifolds;i++)
			{
				manifold = manifoldPtr[i];
				convertContact(manifold,infoGlobal);
			}
		}
	}

	btContactSolverInfo info = infoGlobal;



	int numConstraintPool = m_tmpSolverContactConstraintPool.size();
	int numFrictionPool = m_tmpSolverContactFrictionConstraintPool.size();

	///@todo: use stack allocator for such temporarily memory, same for solver bodies/constraints
	m_orderTmpConstraintPool.resize(numConstraintPool);
	m_orderFrictionConstraintPool.resize(numFrictionPool);
	{
		int i;
		for (i=0;i<numConstraintPool;i++)
		{
			m_orderTmpConstraintPool[i] = i;
		}
		for (i=0;i<numFrictionPool;i++)
		{
			m_orderFrictionConstraintPool[i] = i;
		}
	}

	return 0.f;

}

btScalar btSequentialImpulseConstraintSolver::solveGroupCacheFriendlyIterations(btCollisionObject** /*bodies */,int /*numBodies*/,btPersistentManifold** /*manifoldPtr*/, int /*numManifolds*/,btTypedConstraint** constraints,int numConstraints,const btContactSolverInfo& infoGlobal,btIDebugDraw* /*debugDrawer*/,btStackAlloc* /*stackAlloc*/)
{
	BT_PROFILE("solveGroupCacheFriendlyIterations");

	int numConstraintPool = m_tmpSolverContactConstraintPool.size();
	int numFrictionPool = m_tmpSolverContactFrictionConstraintPool.size();

	//should traverse the contacts random order...
	int iteration;
	{
		for ( iteration = 0;iteration<infoGlobal.m_numIterations;iteration++)
		{			

			int j;
			if (infoGlobal.m_solverMode & SOLVER_RANDMIZE_ORDER)
			{
				if ((iteration & 7) == 0) {
					for (j=0; j<numConstraintPool; ++j) {
						int tmp = m_orderTmpConstraintPool[j];
						int swapi = btRandInt2(j+1);
						m_orderTmpConstraintPool[j] = m_orderTmpConstraintPool[swapi];
						m_orderTmpConstraintPool[swapi] = tmp;
					}

					for (j=0; j<numFrictionPool; ++j) {
						int tmp = m_orderFrictionConstraintPool[j];
						int swapi = btRandInt2(j+1);
						m_orderFrictionConstraintPool[j] = m_orderFrictionConstraintPool[swapi];
						m_orderFrictionConstraintPool[swapi] = tmp;
					}
				}
			}

			if (infoGlobal.m_solverMode & SOLVER_SIMD)
			{
				///solve all joint constraints, using SIMD, if available
				for (j=0;j<m_tmpSolverNonContactConstraintPool.size();j++)
				{
					btSolverConstraint& constraint = m_tmpSolverNonContactConstraintPool[j];
					openmmdLogResolve("noncontact", iteration, j, m_tmpSolverBodyPool[constraint.m_solverBodyIdA],m_tmpSolverBodyPool[constraint.m_solverBodyIdB],constraint);
					resolveSingleConstraintRowGenericSIMD(m_tmpSolverBodyPool[constraint.m_solverBodyIdA],m_tmpSolverBodyPool[constraint.m_solverBodyIdB],constraint);
					openmmdLogResolve("postnc", iteration, j, m_tmpSolverBodyPool[constraint.m_solverBodyIdA],m_tmpSolverBodyPool[constraint.m_solverBodyIdB],constraint);
				}

				for (j=0;j<numConstraints;j++)
				{
					int bodyAid = getOrInitSolverBody(constraints[j]->getRigidBodyA());
					int bodyBid = getOrInitSolverBody(constraints[j]->getRigidBodyB());
					btSolverBody& bodyA = m_tmpSolverBodyPool[bodyAid];
					btSolverBody& bodyB = m_tmpSolverBodyPool[bodyBid];
					openmmdLogJoint(iteration, j, constraints[j], bodyA, bodyB);
					constraints[j]->solveConstraintObsolete(bodyA,bodyB,infoGlobal.m_timeStep);
				}

				///solve all contact constraints using SIMD, if available
				int numPoolConstraints = m_tmpSolverContactConstraintPool.size();
				for (j=0;j<numPoolConstraints;j++)
				{
					const btSolverConstraint& solveManifold = m_tmpSolverContactConstraintPool[m_orderTmpConstraintPool[j]];
					openmmdLogResolve("contact", iteration, j, m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
					resolveSingleConstraintRowLowerLimitSIMD(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
					openmmdLogResolve("postct", iteration, j, m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);

				}
				///solve all friction constraints, using SIMD, if available
				int numFrictionPoolConstraints = m_tmpSolverContactFrictionConstraintPool.size();
				for (j=0;j<numFrictionPoolConstraints;j++)
				{
					btSolverConstraint& solveManifold = m_tmpSolverContactFrictionConstraintPool[m_orderFrictionConstraintPool[j]];
					btScalar totalImpulse = m_tmpSolverContactConstraintPool[solveManifold.m_frictionIndex].m_appliedImpulse;

					if (totalImpulse>btScalar(0))
					{
						solveManifold.m_lowerLimit = -(solveManifold.m_friction*totalImpulse);
						solveManifold.m_upperLimit = solveManifold.m_friction*totalImpulse;

						openmmdLogResolve("friction", iteration, j, m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],	m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
						resolveSingleConstraintRowGenericSIMD(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],	m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
					}
				}
			} else
			{

				///solve all joint constraints
				for (j=0;j<m_tmpSolverNonContactConstraintPool.size();j++)
				{
					btSolverConstraint& constraint = m_tmpSolverNonContactConstraintPool[j];
					resolveSingleConstraintRowGeneric(m_tmpSolverBodyPool[constraint.m_solverBodyIdA],m_tmpSolverBodyPool[constraint.m_solverBodyIdB],constraint);
				}

				for (j=0;j<numConstraints;j++)
				{
					int bodyAid = getOrInitSolverBody(constraints[j]->getRigidBodyA());
					int bodyBid = getOrInitSolverBody(constraints[j]->getRigidBodyB());
					btSolverBody& bodyA = m_tmpSolverBodyPool[bodyAid];
					btSolverBody& bodyB = m_tmpSolverBodyPool[bodyBid];

					constraints[j]->solveConstraintObsolete(bodyA,bodyB,infoGlobal.m_timeStep);
				}

				///solve all contact constraints
				int numPoolConstraints = m_tmpSolverContactConstraintPool.size();
				for (j=0;j<numPoolConstraints;j++)
				{
					const btSolverConstraint& solveManifold = m_tmpSolverContactConstraintPool[m_orderTmpConstraintPool[j]];
					resolveSingleConstraintRowLowerLimit(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
				}
				///solve all friction constraints
				int numFrictionPoolConstraints = m_tmpSolverContactFrictionConstraintPool.size();
				for (j=0;j<numFrictionPoolConstraints;j++)
				{
					btSolverConstraint& solveManifold = m_tmpSolverContactFrictionConstraintPool[m_orderFrictionConstraintPool[j]];
					btScalar totalImpulse = m_tmpSolverContactConstraintPool[solveManifold.m_frictionIndex].m_appliedImpulse;

					if (totalImpulse>btScalar(0))
					{
						solveManifold.m_lowerLimit = -(solveManifold.m_friction*totalImpulse);
						solveManifold.m_upperLimit = solveManifold.m_friction*totalImpulse;

						resolveSingleConstraintRowGeneric(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],							m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
					}
				}
			}

		}

			if (infoGlobal.m_splitImpulse)
			{
				if (infoGlobal.m_solverMode & SOLVER_SIMD)
				{
					for ( iteration = 0;iteration<infoGlobal.m_numIterations;iteration++)
					{
						{
							int numPoolConstraints = m_tmpSolverContactConstraintPool.size();
							int j;
							for (j=0;j<numPoolConstraints;j++)
							{
								const btSolverConstraint& solveManifold = m_tmpSolverContactConstraintPool[m_orderTmpConstraintPool[j]];

								resolveSplitPenetrationSIMD(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],
									m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
							}
						}
					}
				}
				else
				{
					for ( iteration = 0;iteration<infoGlobal.m_numIterations;iteration++)
					{
						{
							int numPoolConstraints = m_tmpSolverContactConstraintPool.size();
							int j;
							for (j=0;j<numPoolConstraints;j++)
							{
								const btSolverConstraint& solveManifold = m_tmpSolverContactConstraintPool[m_orderTmpConstraintPool[j]];

								resolveSplitPenetrationImpulseCacheFriendly(m_tmpSolverBodyPool[solveManifold.m_solverBodyIdA],
									m_tmpSolverBodyPool[solveManifold.m_solverBodyIdB],solveManifold);
							}
						}
					}
				}
			}
		
	}
	return 0.f;
}



/// btSequentialImpulseConstraintSolver Sequentially applies impulses
btScalar btSequentialImpulseConstraintSolver::solveGroup(btCollisionObject** bodies,int numBodies,btPersistentManifold** manifoldPtr, int numManifolds,btTypedConstraint** constraints,int numConstraints,const btContactSolverInfo& infoGlobal,btIDebugDraw* debugDrawer,btStackAlloc* stackAlloc,btDispatcher* /*dispatcher*/)
{

	

	BT_PROFILE("solveGroup");
	//we only implement SOLVER_CACHE_FRIENDLY now
	//you need to provide at least some bodies
	btAssert(bodies);
	btAssert(numBodies);

	// OpenMMD task#8: the event trace covers the first OPENMMD_DUMP_SOLVES
	// solveGroup calls (default 1).
	static int openmmdSolveLimit = -2;
	if (openmmdSolveLimit == -2) {
		const char* limitText = getenv("OPENMMD_DUMP_SOLVES");
		openmmdSolveLimit = (limitText && *limitText) ? atoi(limitText) : 1;
	}
	++g_openmmdSolveCount;
	if (g_openmmdSolveCount > openmmdSolveLimit && g_openmmdLog)
	{
		fflush(g_openmmdLog);
		fclose(g_openmmdLog);
		g_openmmdLog = 0;
		g_openmmdDisabled = true;
	}

	int i;

	solveGroupCacheFriendlySetup( bodies, numBodies, manifoldPtr,  numManifolds,constraints, numConstraints,infoGlobal,debugDrawer, stackAlloc);
	solveGroupCacheFriendlyIterations(bodies, numBodies, manifoldPtr,  numManifolds,constraints, numConstraints,infoGlobal,debugDrawer, stackAlloc);

	int numPoolConstraints = m_tmpSolverContactConstraintPool.size();
	int j;

	for (j=0;j<numPoolConstraints;j++)
	{

		const btSolverConstraint& solveManifold = m_tmpSolverContactConstraintPool[j];
		btManifoldPoint* pt = (btManifoldPoint*) solveManifold.m_originalContactPoint;
		btAssert(pt);
		pt->m_appliedImpulse = solveManifold.m_appliedImpulse;
		if (infoGlobal.m_solverMode & SOLVER_USE_FRICTION_WARMSTARTING)
		{
			pt->m_appliedImpulseLateral1 = m_tmpSolverContactFrictionConstraintPool[solveManifold.m_frictionIndex].m_appliedImpulse;
			pt->m_appliedImpulseLateral2 = m_tmpSolverContactFrictionConstraintPool[solveManifold.m_frictionIndex+1].m_appliedImpulse;
		}

		//do a callback here?
	}

	if (infoGlobal.m_splitImpulse)
	{
		for ( i=0;i<m_tmpSolverBodyPool.size();i++)
		{
			openmmdLogWriteback(m_tmpSolverBodyPool[i]);
			m_tmpSolverBodyPool[i].writebackVelocity(infoGlobal.m_timeStep);
		}
	} else
	{
		for ( i=0;i<m_tmpSolverBodyPool.size();i++)
		{
			openmmdLogWriteback(m_tmpSolverBodyPool[i]);
			m_tmpSolverBodyPool[i].writebackVelocity();
		}
	}


	m_tmpSolverBodyPool.resize(0);
	m_tmpSolverContactConstraintPool.resize(0);
	m_tmpSolverNonContactConstraintPool.resize(0);
	m_tmpSolverContactFrictionConstraintPool.resize(0);

	return 0.f;
}









void	btSequentialImpulseConstraintSolver::reset()
{
	m_btSeed2 = 0;
}


