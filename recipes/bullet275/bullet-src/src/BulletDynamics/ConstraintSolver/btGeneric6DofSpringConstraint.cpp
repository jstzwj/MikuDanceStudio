/*
Bullet Continuous Collision Detection and Physics Library, http://bulletphysics.org
Copyright (C) 2006, 2007 Sony Computer Entertainment Inc. 

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include "btGeneric6DofSpringConstraint.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "LinearMath/btTransformUtil.h"

// Env-gated (OPENMMD_DUMP_SPRING=<path>) per-axis trace of the spring
// parameter refresh: the calculated diff / equilibrium / stiffness /
// damping inputs and the targetVelocity / maxMotorForce outputs.  The
// original (0x500990) reads no body state here, so these six scalars per
// axis fully determine the motor-row inputs downstream in getInfo2.
#include <cstdio>
#include <cstdlib>
#include <cstring>
static int mmdSpringEnabled()
{
	static int state = -1;
	if (state < 0)
		state = getenv("OPENMMD_DUMP_SPRING") ? 1 : 0;
	return state;
}
static FILE* mmdSpringStream()
{
	static FILE* stream = 0;
	if (!stream)
		stream = fopen(getenv("OPENMMD_DUMP_SPRING"), "w");
	return stream;
}
static unsigned mmdSpringBits(float v)
{
	unsigned b;
	memcpy(&b, &v, 4);
	return b;
}


btGeneric6DofSpringConstraint::btGeneric6DofSpringConstraint(btRigidBody& rbA, btRigidBody& rbB, const btTransform& frameInA, const btTransform& frameInB ,bool useLinearReferenceFrameA)
	: btGeneric6DofConstraint(rbA, rbB, frameInA, frameInB, useLinearReferenceFrameA)
{
	for(int i = 0; i < 6; i++)
	{
		m_springEnabled[i] = false;
		m_equilibriumPoint[i] = btScalar(0.f);
		m_springStiffness[i] = btScalar(0.f);
		m_springDamping[i] = btScalar(1.f);
	}
}


void btGeneric6DofSpringConstraint::enableSpring(int index, bool onOff)
{
	btAssert((index >= 0) && (index < 6));
	m_springEnabled[index] = onOff;
	if(index < 3)
	{
		m_linearLimits.m_enableMotor[index] = onOff;
	}
	else
	{
		m_angularLimits[index - 3].m_enableMotor = onOff;
	}
}



void btGeneric6DofSpringConstraint::setStiffness(int index, btScalar stiffness)
{
	btAssert((index >= 0) && (index < 6));
	m_springStiffness[index] = stiffness;
}


void btGeneric6DofSpringConstraint::setDamping(int index, btScalar damping)
{
	btAssert((index >= 0) && (index < 6));
	m_springDamping[index] = damping;
}


void btGeneric6DofSpringConstraint::setEquilibriumPoint()
{
	calculateTransforms();
	for(int i = 0; i < 3; i++)
	{
		m_equilibriumPoint[i] = m_calculatedLinearDiff[i];
	}
	for(int i = 0; i < 3; i++)
	{
		m_equilibriumPoint[i + 3] = m_calculatedAxisAngleDiff[i];
	}
}



void btGeneric6DofSpringConstraint::setEquilibriumPoint(int index)
{
	btAssert((index >= 0) && (index < 6));
	calculateTransforms();
	if(index < 3)
	{
		m_equilibriumPoint[index] = m_calculatedLinearDiff[index];
	}
	else
	{
		m_equilibriumPoint[index + 3] = m_calculatedAxisAngleDiff[index];
	}
}



void btGeneric6DofSpringConstraint::internalUpdateSprings(btConstraintInfo2* info)
{
	// it is assumed that calculateTransforms() have been called before this call
	int i;
	btVector3 relVel = m_rbB.getLinearVelocity() - m_rbA.getLinearVelocity();
	FILE* dump = mmdSpringEnabled() ? mmdSpringStream() : 0;
	int bodyA_id = 0, bodyB_id = 0;
	if (dump) {
		// MMD-ABI rigid ids: btTypedConstraint m_rbA/m_rbB (+32/+36,
		// probe-verified layout) -> btRigidBody m_debugBodyId (+0x224).
		void* rbA = *(void**)((char*)this + 32);
		void* rbB = *(void**)((char*)this + 36);
		if (rbA) bodyA_id = *(int*)((char*)rbA + 0x224);
		if (rbB) bodyB_id = *(int*)((char*)rbB + 0x224);
	}
	for(i = 0; i < 3; i++)
	{
		if(m_springEnabled[i])
		{
			// get current position of constraint
			btScalar currPos = m_calculatedLinearDiff[i];
			// calculate difference
			btScalar delta = currPos - m_equilibriumPoint[i];
			// spring force is (delta * m_stiffness) according to Hooke's Law
			btScalar force = delta * m_springStiffness[i];
			btScalar velFactor = info->fps * m_springDamping[i] / btScalar(info->m_numIterations);
			m_linearLimits.m_targetVelocity[i] =  velFactor * force;
			m_linearLimits.m_maxMotorForce[i] =  btFabs(force) / info->fps;
			if (dump)
				fprintf(dump, "lin uid=%d ba=%d bb=%d ax=%d curr=%08X equi=%08X stif=%08X dmp=%08X dlt=%08X frc=%08X vf=%08X tv=%08X mf=%08X fps=%08X ni=%d\n",
				        getUid(), bodyA_id, bodyB_id, i, mmdSpringBits(currPos), mmdSpringBits(m_equilibriumPoint[i]),
				        mmdSpringBits(m_springStiffness[i]), mmdSpringBits(m_springDamping[i]),
				        mmdSpringBits(delta), mmdSpringBits(force), mmdSpringBits(velFactor),
				        mmdSpringBits(m_linearLimits.m_targetVelocity[i]),
				        mmdSpringBits(m_linearLimits.m_maxMotorForce[i]),
				        mmdSpringBits(info->fps), info->m_numIterations);
		}
	}
	for(i = 0; i < 3; i++)
	{
		if(m_springEnabled[i + 3])
		{
			// get current position of constraint
			btScalar currPos = m_calculatedAxisAngleDiff[i];
			// calculate difference
			btScalar delta = currPos - m_equilibriumPoint[i+3];
			// spring force is (-delta * m_stiffness) according to Hooke's Law
			btScalar force = -delta * m_springStiffness[i+3];
			btScalar velFactor = info->fps * m_springDamping[i+3] / btScalar(info->m_numIterations);
			m_angularLimits[i].m_targetVelocity = velFactor * force;
			m_angularLimits[i].m_maxMotorForce = btFabs(force) / info->fps;
			if (dump)
				fprintf(dump, "ang uid=%d ba=%d bb=%d ax=%d curr=%08X equi=%08X stif=%08X dmp=%08X dlt=%08X frc=%08X vf=%08X tv=%08X mf=%08X fps=%08X ni=%d\n",
				        getUid(), bodyA_id, bodyB_id, i, mmdSpringBits(currPos), mmdSpringBits(m_equilibriumPoint[i+3]),
				        mmdSpringBits(m_springStiffness[i+3]), mmdSpringBits(m_springDamping[i+3]),
				        mmdSpringBits(delta), mmdSpringBits(force), mmdSpringBits(velFactor),
				        mmdSpringBits(m_angularLimits[i].m_targetVelocity),
				        mmdSpringBits(m_angularLimits[i].m_maxMotorForce),
				        mmdSpringBits(info->fps), info->m_numIterations);
		}
	}
	if (dump)
	{
		// Raw constraint state block (linear limits, angular limit motors,
		// all six axis diffs - including axes whose springs are disabled)
		// so a word-level diff against the original localises the exact
		// field of any per-frame 1-ULP divergence.
		fprintf(dump, "raw uid=%d ba=%d bb=%d w=", getUid(), bodyA_id, bodyB_id);
		const unsigned* words = reinterpret_cast<const unsigned*>(
		    static_cast<const char*>(static_cast<const void*>(this)) + 0x2A0);
		for (int w = 0; w < 176; ++w)
			fprintf(dump, "%08X ", words[w]);
		fprintf(dump, "\n");
	}
	if (dump)
	{
		// m_calculatedAxis triad for per-value comparison against the
		// original (0x502930 tail) - the axis chain is stock here, so any
		// per-component ULP difference must be fixed per observed value.
		fprintf(dump, "axs uid=%d ba=%d bb=%d a0=%08X %08X %08X a1=%08X %08X %08X a2=%08X %08X %08X\n",
		        getUid(), bodyA_id, bodyB_id,
		        mmdSpringBits(m_calculatedAxis[0].getX()), mmdSpringBits(m_calculatedAxis[0].getY()), mmdSpringBits(m_calculatedAxis[0].getZ()),
		        mmdSpringBits(m_calculatedAxis[1].getX()), mmdSpringBits(m_calculatedAxis[1].getY()), mmdSpringBits(m_calculatedAxis[1].getZ()),
		        mmdSpringBits(m_calculatedAxis[2].getX()), mmdSpringBits(m_calculatedAxis[2].getY()), mmdSpringBits(m_calculatedAxis[2].getZ()));
	}
	if (dump)
	{
		// Absolute body velocities (the rhs chain reads these directly,
		// not the solverBody deltas): linear @+0x140, angular @+0x150.
		const char* rbA = *(const char**)((const char*)static_cast<const void*>(this) + 32);
		const char* rbB = *(const char**)((const char*)static_cast<const void*>(this) + 36);
		if (rbA && rbB) {
			const unsigned* la = reinterpret_cast<const unsigned*>(rbA + 0x140);
			const unsigned* aa = reinterpret_cast<const unsigned*>(rbA + 0x150);
			const unsigned* lb = reinterpret_cast<const unsigned*>(rbB + 0x140);
			const unsigned* ab = reinterpret_cast<const unsigned*>(rbB + 0x150);
			fprintf(dump, "vel uid=%d ba=%d bb=%d Al=%08X %08X %08X Aa=%08X %08X %08X Bl=%08X %08X %08X Bb=%08X %08X %08X\n",
			        getUid(), bodyA_id, bodyB_id,
			        la[0], la[1], la[2], aa[0], aa[1], aa[2],
			        lb[0], lb[1], lb[2], ab[0], ab[1], ab[2]);
		}
	}
	if (dump)
	{
		// The calculated transforms (body transform x joint frame) at solve
		// time: comparing against the original localises the creation chain -
		// identical body transforms with differing calculated transforms
		// means the joint frames themselves differ.
		const btMatrix3x3& ba = m_calculatedTransformA.getBasis();
		const btMatrix3x3& bb = m_calculatedTransformB.getBasis();
		const btVector3& oa = m_calculatedTransformA.getOrigin();
		const btVector3& ob = m_calculatedTransformB.getOrigin();
		fprintf(dump, "ctx uid=%d ba=%d bb=%d A=%08X %08X %08X %08X %08X %08X %08X %08X %08X o=%08X %08X %08X B=%08X %08X %08X %08X %08X %08X %08X %08X %08X o=%08X %08X %08X\n",
		        getUid(), bodyA_id, bodyB_id,
		        mmdSpringBits(ba[0][0]), mmdSpringBits(ba[0][1]), mmdSpringBits(ba[0][2]),
		        mmdSpringBits(ba[1][0]), mmdSpringBits(ba[1][1]), mmdSpringBits(ba[1][2]),
		        mmdSpringBits(ba[2][0]), mmdSpringBits(ba[2][1]), mmdSpringBits(ba[2][2]),
		        mmdSpringBits(oa[0]), mmdSpringBits(oa[1]), mmdSpringBits(oa[2]),
		        mmdSpringBits(bb[0][0]), mmdSpringBits(bb[0][1]), mmdSpringBits(bb[0][2]),
		        mmdSpringBits(bb[1][0]), mmdSpringBits(bb[1][1]), mmdSpringBits(bb[1][2]),
		        mmdSpringBits(bb[2][0]), mmdSpringBits(bb[2][1]), mmdSpringBits(bb[2][2]),
		        mmdSpringBits(ob[0]), mmdSpringBits(ob[1]), mmdSpringBits(ob[2]));
		// and the body transforms feeding the multiply
		const btTransform& wa = m_rbA.getCenterOfMassTransform();
		const btTransform& wb = m_rbB.getCenterOfMassTransform();
		const btMatrix3x3& wba = wa.getBasis();
		const btMatrix3x3& wbb = wb.getBasis();
		fprintf(dump, "wtx uid=%d ba=%d bb=%d A=%08X %08X %08X %08X %08X %08X %08X %08X %08X o=%08X %08X %08X B=%08X %08X %08X %08X %08X %08X %08X %08X %08X o=%08X %08X %08X\n",
		        getUid(), bodyA_id, bodyB_id,
		        mmdSpringBits(wba[0][0]), mmdSpringBits(wba[0][1]), mmdSpringBits(wba[0][2]),
		        mmdSpringBits(wba[1][0]), mmdSpringBits(wba[1][1]), mmdSpringBits(wba[1][2]),
		        mmdSpringBits(wba[2][0]), mmdSpringBits(wba[2][1]), mmdSpringBits(wba[2][2]),
		        mmdSpringBits(wa.getOrigin()[0]), mmdSpringBits(wa.getOrigin()[1]), mmdSpringBits(wa.getOrigin()[2]),
		        mmdSpringBits(wbb[0][0]), mmdSpringBits(wbb[0][1]), mmdSpringBits(wbb[0][2]),
		        mmdSpringBits(wbb[1][0]), mmdSpringBits(wbb[1][1]), mmdSpringBits(wbb[1][2]),
		        mmdSpringBits(wbb[2][0]), mmdSpringBits(wbb[2][1]), mmdSpringBits(wbb[2][2]),
		        mmdSpringBits(wb.getOrigin()[0]), mmdSpringBits(wb.getOrigin()[1]), mmdSpringBits(wb.getOrigin()[2]));
	}
	if (dump) fflush(dump);
}


void btGeneric6DofSpringConstraint::getInfo2(btConstraintInfo2* info)
{
	// this will be called by constraint solver at the constraint setup stage
	// set current motor parameters
	internalUpdateSprings(info);
	// do the rest of job for constraint setup
	btGeneric6DofConstraint::getInfo2(info);
}




