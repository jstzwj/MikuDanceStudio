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
/*
2007-09-09
Refactored by Francisco Le?n
email: projectileman@yahoo.com
http://gimpact.sf.net
*/

#include "btGeneric6DofConstraint.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "LinearMath/btTransformUtil.h"
#include "LinearMath/btTransformUtil.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>



#define D6_USE_OBSOLETE_METHOD false


btGeneric6DofConstraint::btGeneric6DofConstraint()
:btTypedConstraint(D6_CONSTRAINT_TYPE),
m_useLinearReferenceFrameA(true),
m_useSolveConstraintObsolete(D6_USE_OBSOLETE_METHOD)
{
}



btGeneric6DofConstraint::btGeneric6DofConstraint(btRigidBody& rbA, btRigidBody& rbB, const btTransform& frameInA, const btTransform& frameInB, bool useLinearReferenceFrameA)
: btTypedConstraint(D6_CONSTRAINT_TYPE, rbA, rbB)
, m_frameInA(frameInA)
, m_frameInB(frameInB),
m_useLinearReferenceFrameA(useLinearReferenceFrameA),
m_useSolveConstraintObsolete(D6_USE_OBSOLETE_METHOD)
{

}



#define GENERIC_D6_DISABLE_WARMSTARTING 1



btScalar btGetMatrixElem(const btMatrix3x3& mat, int index);
btScalar btGetMatrixElem(const btMatrix3x3& mat, int index)
{
	int i = index%3;
	int j = index/3;
	return mat[i][j];
}



///MatrixToEulerXYZ from http://www.geometrictools.com/LibFoundation/Mathematics/Wm4Matrix3.inl.html
bool	matrixToEulerXYZ(const btMatrix3x3& mat,btVector3& xyz);
bool	matrixToEulerXYZ(const btMatrix3x3& mat,btVector3& xyz)
{
	//	// rot =  cy*cz          -cy*sz           sy
	//	//        cz*sx*sy+cx*sz  cx*cz-sx*sy*sz -cy*sx
	//	//       -cx*cz*sy+sx*sz  cz*sx+cx*sy*sz  cx*cy
	//

	btScalar fi = btGetMatrixElem(mat,2);
	if (fi < btScalar(1.0f))
	{
		if (fi > btScalar(-1.0f))
		{
			/* Original 0x501200 evaluates the arc functions through the
			   CRT double atan2/asin and stores each result to float once;
			   the stock float btAtan2/btAsin differ by 1 ULP on some
			   inputs, which the 6DOF spring chain amplifies per frame. */
			xyz[0] = (float)::atan2((double)-btGetMatrixElem(mat,5),(double)btGetMatrixElem(mat,8));
			xyz[1] = (float)::asin((double)btGetMatrixElem(mat,2));
			xyz[2] = (float)::atan2((double)-btGetMatrixElem(mat,1),(double)btGetMatrixElem(mat,0));
			return true;
		}
		else
		{
			// WARNING.  Not unique.  XA - ZA = -atan2(r10,r11)
			xyz[0] = (float)-::atan2((double)btGetMatrixElem(mat,3),(double)btGetMatrixElem(mat,4));
			xyz[1] = -SIMD_HALF_PI;
			xyz[2] = btScalar(0.0);
			return false;
		}
	}
	else
	{
		// WARNING.  Not unique.  XAngle + ZAngle = atan2(r10,r11)
		xyz[0] = (float)::atan2((double)btGetMatrixElem(mat,3),(double)btGetMatrixElem(mat,4));
		xyz[1] = SIMD_HALF_PI;
		xyz[2] = 0.0;
	}
	return false;
}

//////////////////////////// btRotationalLimitMotor ////////////////////////////////////

int btRotationalLimitMotor::testLimitValue(btScalar test_value)
{
	if(m_loLimit>m_hiLimit)
	{
		m_currentLimit = 0;//Free from violation
		return 0;
	}
	if (test_value < m_loLimit)
	{
		m_currentLimit = 1;//low limit violation
		m_currentLimitError =  test_value - m_loLimit;
		return 1;
	}
	else if (test_value> m_hiLimit)
	{
		m_currentLimit = 2;//High limit violation
		m_currentLimitError = test_value - m_hiLimit;
		return 2;
	};

	m_currentLimit = 0;//Free from violation
	return 0;

}



btScalar btRotationalLimitMotor::solveAngularLimits(
	btScalar timeStep,btVector3& axis,btScalar jacDiagABInv,
	btRigidBody * body0, btSolverBody& bodyA, btRigidBody * body1, btSolverBody& bodyB)
{
	if (needApplyTorques()==false) return 0.0f;

	btScalar target_velocity = m_targetVelocity;
	btScalar maxMotorForce = m_maxMotorForce;

	//current error correction
	if (m_currentLimit!=0)
	{
		target_velocity = -m_ERP*m_currentLimitError/(timeStep);
		maxMotorForce = m_maxLimitForce;
	}

	maxMotorForce *= timeStep;

	// current velocity difference

	btVector3 angVelA;
	bodyA.getAngularVelocity(angVelA);
	btVector3 angVelB;
	bodyB.getAngularVelocity(angVelB);

	btVector3 vel_diff;
	vel_diff = angVelA-angVelB;



	btScalar rel_vel = axis.dot(vel_diff);

	// correction velocity
	btScalar motor_relvel = m_limitSoftness*(target_velocity  - m_damping*rel_vel);


	if ( motor_relvel < SIMD_EPSILON && motor_relvel > -SIMD_EPSILON  )
	{
		return 0.0f;//no need for applying force
	}


	// correction impulse
	btScalar unclippedMotorImpulse = (1+m_bounce)*motor_relvel*jacDiagABInv;

	// clip correction impulse
	btScalar clippedMotorImpulse;

	///@todo: should clip against accumulated impulse
	if (unclippedMotorImpulse>0.0f)
	{
		clippedMotorImpulse =  unclippedMotorImpulse > maxMotorForce? maxMotorForce: unclippedMotorImpulse;
	}
	else
	{
		clippedMotorImpulse =  unclippedMotorImpulse < -maxMotorForce ? -maxMotorForce: unclippedMotorImpulse;
	}


	// sort with accumulated impulses
	btScalar	lo = btScalar(-BT_LARGE_FLOAT);
	btScalar	hi = btScalar(BT_LARGE_FLOAT);

	btScalar oldaccumImpulse = m_accumulatedImpulse;
	btScalar sum = oldaccumImpulse + clippedMotorImpulse;
	m_accumulatedImpulse = sum > hi ? btScalar(0.) : sum < lo ? btScalar(0.) : sum;

	clippedMotorImpulse = m_accumulatedImpulse - oldaccumImpulse;

	btVector3 motorImp = clippedMotorImpulse * axis;

	//body0->applyTorqueImpulse(motorImp);
	//body1->applyTorqueImpulse(-motorImp);

	bodyA.applyImpulse(btVector3(0,0,0), body0->getInvInertiaTensorWorld()*axis,clippedMotorImpulse);
	bodyB.applyImpulse(btVector3(0,0,0), body1->getInvInertiaTensorWorld()*axis,-clippedMotorImpulse);


	return clippedMotorImpulse;


}

//////////////////////////// End btRotationalLimitMotor ////////////////////////////////////




//////////////////////////// btTranslationalLimitMotor ////////////////////////////////////


int btTranslationalLimitMotor::testLimitValue(int limitIndex, btScalar test_value)
{
	btScalar loLimit = m_lowerLimit[limitIndex];
	btScalar hiLimit = m_upperLimit[limitIndex];
	if(loLimit > hiLimit)
	{
		m_currentLimit[limitIndex] = 0;//Free from violation
		m_currentLimitError[limitIndex] = btScalar(0.f);
		return 0;
	}

	if (test_value < loLimit)
	{
		m_currentLimit[limitIndex] = 2;//low limit violation
		m_currentLimitError[limitIndex] =  test_value - loLimit;
		return 2;
	}
	else if (test_value> hiLimit)
	{
		m_currentLimit[limitIndex] = 1;//High limit violation
		m_currentLimitError[limitIndex] = test_value - hiLimit;
		return 1;
	};

	m_currentLimit[limitIndex] = 0;//Free from violation
	m_currentLimitError[limitIndex] = btScalar(0.f);
	return 0;
}



btScalar btTranslationalLimitMotor::solveLinearAxis(
	btScalar timeStep,
	btScalar jacDiagABInv,
	btRigidBody& body1,btSolverBody& bodyA,const btVector3 &pointInA,
	btRigidBody& body2,btSolverBody& bodyB,const btVector3 &pointInB,
	int limit_index,
	const btVector3 & axis_normal_on_a,
	const btVector3 & anchorPos)
{

	///find relative velocity
	//    btVector3 rel_pos1 = pointInA - body1.getCenterOfMassPosition();
	//    btVector3 rel_pos2 = pointInB - body2.getCenterOfMassPosition();
	btVector3 rel_pos1 = anchorPos - body1.getCenterOfMassPosition();
	btVector3 rel_pos2 = anchorPos - body2.getCenterOfMassPosition();

	btVector3 vel1;
	bodyA.getVelocityInLocalPointObsolete(rel_pos1,vel1);
	btVector3 vel2;
	bodyB.getVelocityInLocalPointObsolete(rel_pos2,vel2);
	btVector3 vel = vel1 - vel2;

	btScalar rel_vel = axis_normal_on_a.dot(vel);



	/// apply displacement correction

	//positional error (zeroth order error)
	btScalar depth = -(pointInA - pointInB).dot(axis_normal_on_a);
	btScalar	lo = btScalar(-BT_LARGE_FLOAT);
	btScalar	hi = btScalar(BT_LARGE_FLOAT);

	btScalar minLimit = m_lowerLimit[limit_index];
	btScalar maxLimit = m_upperLimit[limit_index];

	//handle the limits
	if (minLimit < maxLimit)
	{
		{
			if (depth > maxLimit)
			{
				depth -= maxLimit;
				lo = btScalar(0.);

			}
			else
			{
				if (depth < minLimit)
				{
					depth -= minLimit;
					hi = btScalar(0.);
				}
				else
				{
					return 0.0f;
				}
			}
		}
	}

	btScalar normalImpulse= m_limitSoftness*(m_restitution*depth/timeStep - m_damping*rel_vel) * jacDiagABInv;




	btScalar oldNormalImpulse = m_accumulatedImpulse[limit_index];
	btScalar sum = oldNormalImpulse + normalImpulse;
	m_accumulatedImpulse[limit_index] = sum > hi ? btScalar(0.) : sum < lo ? btScalar(0.) : sum;
	normalImpulse = m_accumulatedImpulse[limit_index] - oldNormalImpulse;

	btVector3 impulse_vector = axis_normal_on_a * normalImpulse;
	//body1.applyImpulse( impulse_vector, rel_pos1);
	//body2.applyImpulse(-impulse_vector, rel_pos2);

	btVector3 ftorqueAxis1 = rel_pos1.cross(axis_normal_on_a);
	btVector3 ftorqueAxis2 = rel_pos2.cross(axis_normal_on_a);
	bodyA.applyImpulse(axis_normal_on_a*body1.getInvMass(), body1.getInvInertiaTensorWorld()*ftorqueAxis1,normalImpulse);
	bodyB.applyImpulse(axis_normal_on_a*body2.getInvMass(), body2.getInvInertiaTensorWorld()*ftorqueAxis2,-normalImpulse);




	return normalImpulse;
}

//////////////////////////// btTranslationalLimitMotor ////////////////////////////////////

void btGeneric6DofConstraint::calculateAngleInfo()
{
	/* The original 0x502930 computes the whole relative-frame chain in
	   scalar SSE with per-operation rounding - identical to the stock
	   inverse/multiply - EXCEPT the determinant's addition grouping:
	   (co2*A02 + A01*co1) + A00*co0 instead of the stock left-associative
	   (A00*co0 + A01*co1) + A02*co2.  That 1-ULP difference in 1/det
	   shifts the relative frame and the extracted angles (proven offline
	   on the (5,6) joint's frame-2 state: det 3F7FFFFB vs 3F7FFFFC gives
	   the original's ang0 BCB92D7E instead of the stock BCB92D7F). */
	const btMatrix3x3& A = m_calculatedTransformA.getBasis();
	const btMatrix3x3& B = m_calculatedTransformB.getBasis();
	btMatrix3x3 invA;
	{
		const btScalar co0 = A[1][1]*A[2][2] - A[1][2]*A[2][1];
		const btScalar co1 = A[2][0]*A[1][2] - A[1][0]*A[2][2];
		const btScalar co2 = A[1][0]*A[2][1] - A[2][0]*A[1][1];
		const btScalar det = (co2*A[0][2] + A[0][1]*co1) + A[0][0]*co0;
		const btScalar s = btScalar(1.0)/det;
		invA.setValue(
			co0*s,
			(A[0][2]*A[2][1] - A[0][1]*A[2][2])*s,
			(A[0][1]*A[1][2] - A[0][2]*A[1][1])*s,
			co1*s,
			(A[0][0]*A[2][2] - A[0][2]*A[2][0])*s,
			(A[0][2]*A[1][0] - A[0][0]*A[1][2])*s,
			co2*s,
			(A[0][1]*A[2][0] - A[0][0]*A[2][1])*s,
			(A[0][0]*A[1][1] - A[0][1]*A[1][0])*s);
	}
	/* Relative-frame multiply with the original's per-element addition
	   groupings (0x502930 codegen): column 0 folds left-associative,
	   columns 1/2 of rows 0-1 fold (p0+p2)+p1, and row 2 folds
	   (p0+p1)+p2 for column 1 but (p1+p2)+p0 for column 2.  Verified
	   offline against both observed divergent samples ((5,6) and (7,8)
	   joints, all four angle words each). */
	btMatrix3x3 relative_frame;
	relative_frame[0][0] = (invA[0][0]*B[0][0] + invA[0][1]*B[1][0]) + invA[0][2]*B[2][0];
	relative_frame[0][1] = (invA[0][0]*B[0][1] + invA[0][2]*B[2][1]) + invA[0][1]*B[1][1];
	relative_frame[0][2] = (invA[0][0]*B[0][2] + invA[0][2]*B[2][2]) + invA[0][1]*B[1][2];
	relative_frame[1][0] = (invA[1][0]*B[0][0] + invA[1][1]*B[1][0]) + invA[1][2]*B[2][0];
	relative_frame[1][1] = (invA[1][0]*B[0][1] + invA[1][2]*B[2][1]) + invA[1][1]*B[1][1];
	relative_frame[1][2] = (invA[1][0]*B[0][2] + invA[1][2]*B[2][2]) + invA[1][1]*B[1][2];
	relative_frame[2][0] = (invA[2][0]*B[0][0] + invA[2][1]*B[1][0]) + invA[2][2]*B[2][0];
	relative_frame[2][1] = (invA[2][0]*B[0][1] + invA[2][1]*B[1][1]) + invA[2][2]*B[2][1];
	relative_frame[2][2] = (invA[2][1]*B[1][2] + invA[2][2]*B[2][2]) + invA[2][0]*B[0][2];
	matrixToEulerXYZ(relative_frame,m_calculatedAxisAngleDiff);
	// in euler angle mode we do not actually constrain the angular velocity
	// along the axes axis[0] and axis[2] (although we do use axis[1]) :
	//
	//    to get			constrain w2-w1 along		...not
	//    ------			---------------------		------
	//    d(angle[0])/dt = 0	ax[1] x ax[2]			ax[0]
	//    d(angle[1])/dt = 0	ax[1]
	//    d(angle[2])/dt = 0	ax[0] x ax[1]			ax[2]
	//
	// constraining w2-w1 along an axis 'a' means that a'*(w2-w1)=0.
	// to prove the result for angle[0], write the expression for angle[0] from
	// GetInfo1 then take the derivative. to prove this for angle[2] it is
	// easier to take the euler rate expression for d(angle[2])/dt with respect
	// to the components of w and set that to 0.
	/* Axes transcribed at instruction level from 0x502930's tail: the
	   cross products are pure SSE (per-op float rounding), but each
	   normalization computes x^2+y^2+z^2, sqrt and the reciprocal on the
	   x87 stack (exact products, single rounding of the stored
	   reciprocal) and multiplies per component with mulss.  The earlier
	   all-extended and all-SSE attempts each missed one half of this. */
	{
		const btMatrix3x3& Ax = m_calculatedTransformA.getBasis();
		const btMatrix3x3& Bx = m_calculatedTransformB.getBasis();
		const float a1x = Ax[1][2]*Bx[2][0] - Ax[2][2]*Bx[1][0];
		const float a1y = Ax[2][2]*Bx[0][0] - Bx[2][0]*Ax[0][2];
		const float a1z = Bx[1][0]*Ax[0][2] - Ax[1][2]*Bx[0][0];
		const float a0x = Ax[2][2]*a1y - Ax[1][2]*a1z;
		const float a0y = Ax[0][2]*a1z - Ax[2][2]*a1x;
		const float a0z = Ax[1][2]*a1x - Ax[0][2]*a1y;
		const float a2x = Bx[1][0]*a1z - Bx[2][0]*a1y;
		const float a2y = Bx[2][0]*a1x - a1z*Bx[0][0];
		const float a2z = a1y*Bx[0][0] - Bx[1][0]*a1x;
		typedef long double ld;
		const float n0 = (float)(1.0L / sqrtl((ld)a0x*a0x + (ld)a0y*a0y + (ld)a0z*a0z));
		const float n1 = (float)(1.0L / sqrtl((ld)a1x*a1x + (ld)a1y*a1y + (ld)a1z*a1z));
		const float n2 = (float)(1.0L / sqrtl((ld)a2x*a2x + (ld)a2y*a2y + (ld)a2z*a2z));
		m_calculatedAxis[0].setValue(n0*a0x, n0*a0y, n0*a0z);
		m_calculatedAxis[1].setValue(n1*a1x, n1*a1y, n1*a1z);
		m_calculatedAxis[2].setValue(n2*a2x, n2*a2y, n2*a2z);
	}

}

void btGeneric6DofConstraint::calculateTransforms()
{
	calculateTransforms(m_rbA.getCenterOfMassTransform(),m_rbB.getCenterOfMassTransform());
}

void btGeneric6DofConstraint::calculateTransforms(const btTransform& transA,const btTransform& transB)
{
	/* The original's VC9 codegen (0x503DC0) folds the body x frame matrix
	   product with a per-row addition order that differs from the stock
	   left-associative chain: row 0 accumulates (p0+p1)+p2 like stock, but
	   rows 1 and 2 (and the y/z components of the transformed origin)
	   accumulate (p1+p2)+p0.  With otherwise bit-identical body transforms
	   and joint frames, that fold order alone shifts the calculated
	   transform basis by 1-2 ULP, which the 6DOF spring equilibrium chain
	   then amplifies into a visible frame-4 drift.  Replicated verbatim. */
	const btMatrix3x3& BA = transA.getBasis();
	const btMatrix3x3& FA = m_frameInA.getBasis();
	const btVector3& oFA = m_frameInA.getOrigin();
	const btVector3& oA = transA.getOrigin();
	btMatrix3x3& RA = m_calculatedTransformA.getBasis();
	btVector3& oRA = m_calculatedTransformA.getOrigin();
	RA[0][0] = (FA[0][0]*BA[0][0] + FA[1][0]*BA[0][1]) + FA[2][0]*BA[0][2];
	RA[0][1] = (FA[0][1]*BA[0][0] + FA[1][1]*BA[0][1]) + FA[2][1]*BA[0][2];
	RA[0][2] = (FA[0][2]*BA[0][0] + FA[1][2]*BA[0][1]) + FA[2][2]*BA[0][2];
	RA[1][0] = (FA[1][0]*BA[1][1] + FA[2][0]*BA[1][2]) + FA[0][0]*BA[1][0];
	RA[1][1] = (FA[1][1]*BA[1][1] + FA[2][1]*BA[1][2]) + FA[0][1]*BA[1][0];
	RA[1][2] = (FA[1][2]*BA[1][1] + FA[2][2]*BA[1][2]) + FA[0][2]*BA[1][0];
	RA[2][0] = (FA[1][0]*BA[2][1] + FA[2][0]*BA[2][2]) + FA[0][0]*BA[2][0];
	RA[2][1] = (FA[1][1]*BA[2][1] + FA[2][1]*BA[2][2]) + FA[0][1]*BA[2][0];
	RA[2][2] = (FA[1][2]*BA[2][1] + FA[2][2]*BA[2][2]) + FA[0][2]*BA[2][0];
	oRA[0] = ((BA[0][0]*oFA[0] + BA[0][1]*oFA[1]) + BA[0][2]*oFA[2]) + oA[0];
	oRA[1] = ((BA[1][1]*oFA[1] + BA[1][2]*oFA[2]) + BA[1][0]*oFA[0]) + oA[1];
	oRA[2] = ((BA[2][1]*oFA[1] + BA[2][2]*oFA[2]) + BA[2][0]*oFA[0]) + oA[2];

	const btMatrix3x3& BB = transB.getBasis();
	const btMatrix3x3& FB = m_frameInB.getBasis();
	const btVector3& oFB = m_frameInB.getOrigin();
	const btVector3& oB = transB.getOrigin();
	btMatrix3x3& RB = m_calculatedTransformB.getBasis();
	btVector3& oRB = m_calculatedTransformB.getOrigin();
	RB[0][0] = (FB[0][0]*BB[0][0] + FB[1][0]*BB[0][1]) + FB[2][0]*BB[0][2];
	RB[0][1] = (FB[0][1]*BB[0][0] + FB[1][1]*BB[0][1]) + FB[2][1]*BB[0][2];
	RB[0][2] = (FB[0][2]*BB[0][0] + FB[1][2]*BB[0][1]) + FB[2][2]*BB[0][2];
	RB[1][0] = (FB[1][0]*BB[1][1] + FB[2][0]*BB[1][2]) + FB[0][0]*BB[1][0];
	RB[1][1] = (FB[1][1]*BB[1][1] + FB[2][1]*BB[1][2]) + FB[0][1]*BB[1][0];
	RB[1][2] = (FB[1][2]*BB[1][1] + FB[2][2]*BB[1][2]) + FB[0][2]*BB[1][0];
	RB[2][0] = (FB[1][0]*BB[2][1] + FB[2][0]*BB[2][2]) + FB[0][0]*BB[2][0];
	RB[2][1] = (FB[1][1]*BB[2][1] + FB[2][1]*BB[2][2]) + FB[0][1]*BB[2][0];
	RB[2][2] = (FB[1][2]*BB[2][1] + FB[2][2]*BB[2][2]) + FB[0][2]*BB[2][0];
	oRB[0] = ((BB[0][0]*oFB[0] + BB[0][1]*oFB[1]) + BB[0][2]*oFB[2]) + oB[0];
	oRB[1] = ((BB[1][1]*oFB[1] + BB[1][2]*oFB[2]) + BB[1][0]*oFB[0]) + oB[1];
	oRB[2] = ((BB[2][1]*oFB[1] + BB[2][2]*oFB[2]) + BB[2][0]*oFB[0]) + oB[2];

	calculateLinearInfo();
	calculateAngleInfo();
}



void btGeneric6DofConstraint::buildLinearJacobian(
	btJacobianEntry & jacLinear,const btVector3 & normalWorld,
	const btVector3 & pivotAInW,const btVector3 & pivotBInW)
{
	new (&jacLinear) btJacobianEntry(
        m_rbA.getCenterOfMassTransform().getBasis().transpose(),
        m_rbB.getCenterOfMassTransform().getBasis().transpose(),
        pivotAInW - m_rbA.getCenterOfMassPosition(),
        pivotBInW - m_rbB.getCenterOfMassPosition(),
        normalWorld,
        m_rbA.getInvInertiaDiagLocal(),
        m_rbA.getInvMass(),
        m_rbB.getInvInertiaDiagLocal(),
        m_rbB.getInvMass());
}



void btGeneric6DofConstraint::buildAngularJacobian(
	btJacobianEntry & jacAngular,const btVector3 & jointAxisW)
{
	 new (&jacAngular)	btJacobianEntry(jointAxisW,
                                      m_rbA.getCenterOfMassTransform().getBasis().transpose(),
                                      m_rbB.getCenterOfMassTransform().getBasis().transpose(),
                                      m_rbA.getInvInertiaDiagLocal(),
                                      m_rbB.getInvInertiaDiagLocal());

}



bool btGeneric6DofConstraint::testAngularLimitMotor(int axis_index)
{
	btScalar angle = m_calculatedAxisAngleDiff[axis_index];
	angle = btAdjustAngleToLimits(angle, m_angularLimits[axis_index].m_loLimit, m_angularLimits[axis_index].m_hiLimit);
	m_angularLimits[axis_index].m_currentPosition = angle;
	//test limits
	m_angularLimits[axis_index].testLimitValue(angle);
	return m_angularLimits[axis_index].needApplyTorques();
}



void btGeneric6DofConstraint::buildJacobian()
{
#ifndef __SPU__
	if (m_useSolveConstraintObsolete)
	{

		// Clear accumulated impulses for the next simulation step
		m_linearLimits.m_accumulatedImpulse.setValue(btScalar(0.), btScalar(0.), btScalar(0.));
		int i;
		for(i = 0; i < 3; i++)
		{
			m_angularLimits[i].m_accumulatedImpulse = btScalar(0.);
		}
		//calculates transform
		calculateTransforms(m_rbA.getCenterOfMassTransform(),m_rbB.getCenterOfMassTransform());

		//  const btVector3& pivotAInW = m_calculatedTransformA.getOrigin();
		//  const btVector3& pivotBInW = m_calculatedTransformB.getOrigin();
		calcAnchorPos();
		btVector3 pivotAInW = m_AnchorPos;
		btVector3 pivotBInW = m_AnchorPos;

		// not used here
		//    btVector3 rel_pos1 = pivotAInW - m_rbA.getCenterOfMassPosition();
		//    btVector3 rel_pos2 = pivotBInW - m_rbB.getCenterOfMassPosition();

		btVector3 normalWorld;
		//linear part
		for (i=0;i<3;i++)
		{
			if (m_linearLimits.isLimited(i))
			{
				if (m_useLinearReferenceFrameA)
					normalWorld = m_calculatedTransformA.getBasis().getColumn(i);
				else
					normalWorld = m_calculatedTransformB.getBasis().getColumn(i);

				buildLinearJacobian(
					m_jacLinear[i],normalWorld ,
					pivotAInW,pivotBInW);

			}
		}

		// angular part
		for (i=0;i<3;i++)
		{
			//calculates error angle
			if (testAngularLimitMotor(i))
			{
				normalWorld = this->getAxis(i);
				// Create angular atom
				buildAngularJacobian(m_jacAng[i],normalWorld);
			}
		}

	}
#endif //__SPU__

}


void btGeneric6DofConstraint::getInfo1 (btConstraintInfo1* info)
{
	if (m_useSolveConstraintObsolete)
	{
		info->m_numConstraintRows = 0;
		info->nub = 0;
	} else
	{
		//prepare constraint
		calculateTransforms(m_rbA.getCenterOfMassTransform(),m_rbB.getCenterOfMassTransform());
		info->m_numConstraintRows = 0;
		info->nub = 6;
		int i;
		//test linear limits
		for(i = 0; i < 3; i++)
		{
			if(m_linearLimits.needApplyForce(i))
			{
				info->m_numConstraintRows++;
				info->nub--;
			}
		}
		//test angular limits
		for (i=0;i<3 ;i++ )
		{
			if(testAngularLimitMotor(i))
			{
				info->m_numConstraintRows++;
				info->nub--;
			}
		}
	}
}

void btGeneric6DofConstraint::getInfo1NonVirtual (btConstraintInfo1* info)
{
	if (m_useSolveConstraintObsolete)
	{
		info->m_numConstraintRows = 0;
		info->nub = 0;
	} else
	{
		//pre-allocate all 6
		info->m_numConstraintRows = 6;
		info->nub = 0;
	}
}


void btGeneric6DofConstraint::getInfo2 (btConstraintInfo2* info)
{
	getInfo2NonVirtual(info,m_rbA.getCenterOfMassTransform(),m_rbB.getCenterOfMassTransform(), m_rbA.getLinearVelocity(),m_rbB.getLinearVelocity(),m_rbA.getAngularVelocity(), m_rbB.getAngularVelocity());
}

void btGeneric6DofConstraint::getInfo2NonVirtual (btConstraintInfo2* info, const btTransform& transA,const btTransform& transB,const btVector3& linVelA,const btVector3& linVelB,const btVector3& angVelA,const btVector3& angVelB)
{
	btAssert(!m_useSolveConstraintObsolete);

	//prepare constraint
	calculateTransforms(transA,transB);
	
	int i;
	//test linear limits
	for(i = 0; i < 3; i++)
	{
		if(m_linearLimits.needApplyForce(i))
		{
	
		}
	}
	//test angular limits
	for (i=0;i<3 ;i++ )
	{
		if(testAngularLimitMotor(i))
		{
	
		}
	}

	int row = setLinearLimits(info,transA,transB,linVelA,linVelB,angVelA,angVelB);
	setAngularLimits(info, row,transA,transB,linVelA,linVelB,angVelA,angVelB);
}



int btGeneric6DofConstraint::setLinearLimits(btConstraintInfo2* info,const btTransform& transA,const btTransform& transB,const btVector3& linVelA,const btVector3& linVelB,const btVector3& angVelA,const btVector3& angVelB)
{
	int row = 0;
	//solve linear limits
	btRotationalLimitMotor limot;
	for (int i=0;i<3 ;i++ )
	{
		if(m_linearLimits.needApplyForce(i))
		{ // re-use rotational motor code
			limot.m_bounce = btScalar(0.f);
			limot.m_currentLimit = m_linearLimits.m_currentLimit[i];
			limot.m_currentPosition = m_linearLimits.m_currentLinearDiff[i];
			limot.m_currentLimitError  = m_linearLimits.m_currentLimitError[i];
			limot.m_damping  = m_linearLimits.m_damping;
			limot.m_enableMotor  = m_linearLimits.m_enableMotor[i];
			limot.m_ERP  = m_linearLimits.m_restitution;
			limot.m_hiLimit  = m_linearLimits.m_upperLimit[i];
			limot.m_limitSoftness  = m_linearLimits.m_limitSoftness;
			limot.m_loLimit  = m_linearLimits.m_lowerLimit[i];
			limot.m_maxLimitForce  = btScalar(0.f);
			limot.m_maxMotorForce  = m_linearLimits.m_maxMotorForce[i];
			limot.m_targetVelocity  = m_linearLimits.m_targetVelocity[i];
			btVector3 axis = m_calculatedTransformA.getBasis().getColumn(i);
			row += get_limit_motor_info2(&limot, 
				transA,transB,linVelA,linVelB,angVelA,angVelB
				, info, row, axis, 0);
		}
	}
	return row;
}



int btGeneric6DofConstraint::setAngularLimits(btConstraintInfo2 *info, int row_offset, const btTransform& transA,const btTransform& transB,const btVector3& linVelA,const btVector3& linVelB,const btVector3& angVelA,const btVector3& angVelB)
{
	btGeneric6DofConstraint * d6constraint = this;
	int row = row_offset;
	//solve angular limits
	for (int i=0;i<3 ;i++ )
	{
		if(d6constraint->getRotationalLimitMotor(i)->needApplyTorques())
		{
			btVector3 axis = d6constraint->getAxis(i);
			row += get_limit_motor_info2(
				d6constraint->getRotationalLimitMotor(i),
				transA,transB,linVelA,linVelB,angVelA,angVelB,
				info,row,axis,1);
		}
	}

	return row;
}



void btGeneric6DofConstraint::solveConstraintObsolete(btSolverBody& bodyA,btSolverBody& bodyB,btScalar	timeStep)
{
	if (m_useSolveConstraintObsolete)
	{


		m_timeStep = timeStep;

		//calculateTransforms();

		int i;

		// linear

		btVector3 pointInA = m_calculatedTransformA.getOrigin();
		btVector3 pointInB = m_calculatedTransformB.getOrigin();

		btScalar jacDiagABInv;
		btVector3 linear_axis;
		for (i=0;i<3;i++)
		{
			if (m_linearLimits.isLimited(i))
			{
				jacDiagABInv = btScalar(1.) / m_jacLinear[i].getDiagonal();

				if (m_useLinearReferenceFrameA)
					linear_axis = m_calculatedTransformA.getBasis().getColumn(i);
				else
					linear_axis = m_calculatedTransformB.getBasis().getColumn(i);

				m_linearLimits.solveLinearAxis(
					m_timeStep,
					jacDiagABInv,
					m_rbA,bodyA,pointInA,
					m_rbB,bodyB,pointInB,
					i,linear_axis, m_AnchorPos);

			}
		}

		// angular
		btVector3 angular_axis;
		btScalar angularJacDiagABInv;
		for (i=0;i<3;i++)
		{
			if (m_angularLimits[i].needApplyTorques())
			{

				// get axis
				angular_axis = getAxis(i);

				angularJacDiagABInv = btScalar(1.) / m_jacAng[i].getDiagonal();

				m_angularLimits[i].solveAngularLimits(m_timeStep,angular_axis,angularJacDiagABInv, &m_rbA,bodyA,&m_rbB,bodyB);
			}
		}
	}
}



void	btGeneric6DofConstraint::updateRHS(btScalar	timeStep)
{
	(void)timeStep;

}



btVector3 btGeneric6DofConstraint::getAxis(int axis_index) const
{
	return m_calculatedAxis[axis_index];
}


btScalar	btGeneric6DofConstraint::getRelativePivotPosition(int axisIndex) const
{
	return m_calculatedLinearDiff[axisIndex];
}


btScalar btGeneric6DofConstraint::getAngle(int axisIndex) const
{
	return m_calculatedAxisAngleDiff[axisIndex];
}



void btGeneric6DofConstraint::calcAnchorPos(void)
{
	btScalar imA = m_rbA.getInvMass();
	btScalar imB = m_rbB.getInvMass();
	btScalar weight;
	if(imB == btScalar(0.0))
	{
		weight = btScalar(1.0);
	}
	else
	{
		weight = imA / (imA + imB);
	}
	const btVector3& pA = m_calculatedTransformA.getOrigin();
	const btVector3& pB = m_calculatedTransformB.getOrigin();
	m_AnchorPos = pA * weight + pB * (btScalar(1.0) - weight);
	return;
}



void btGeneric6DofConstraint::calculateLinearInfo()
{
	/* Verbatim transcription of the original's linear projection
	   (0x503980 second half): the inverse entries and the projection
	   products round exactly where the VC9 build rounds them, with its
	   per-row addition grouping - row 0 folds (p1+p2)+p3 with dz*inv
	   operand orders, rows 1/2 use the adjugate slots the scheduler
	   picked.  With bit-identical calculated transforms this chain is
	   what shifts m_calculatedLinearDiff (and every locked-linear-axis
	   row error) by 1 ULP against the stock inverse()*diff. */
	m_calculatedLinearDiff = m_calculatedTransformB.getOrigin() - m_calculatedTransformA.getOrigin();
	{
		const btMatrix3x3& A = m_calculatedTransformA.getBasis();
		const btScalar dx = m_calculatedLinearDiff[0];
		const btScalar dy = m_calculatedLinearDiff[1];
		const btScalar dz = m_calculatedLinearDiff[2];
		// adjugate intermediates exactly as spilled at 0x503980
		const btScalar v6 = A[1][1]*A[2][2] - A[1][2]*A[2][1];
		const btScalar v7 = A[2][0]*A[1][2] - A[1][0]*A[2][2];
		const btScalar v8 = A[1][0]*A[2][1] - A[2][0]*A[1][1];
		const btScalar v9 = btScalar(1.0) /
			((A[0][1]*v7 + v8*A[0][2]) + v6*A[0][0]);
		const btScalar v12 = (A[0][1]*A[2][0] - A[0][0]*A[2][1]) * v9;
		const btScalar v29 = (A[0][2]*A[1][0] - A[0][0]*A[1][2]) * v9;
		const btScalar v30 = (A[0][0]*A[2][2] - A[0][2]*A[2][0]) * v9;
		const btScalar v31 = v8 * v9;
		const btScalar v32 = (A[0][0]*A[1][1] - A[0][1]*A[1][0]) * v9;
		// row 0's dz term multiplies the PRE-SCALED cofactor slot
		// (dz*(c01*v9)), exactly like rows 1/2 use v29/v30/v32 - the
		// left-associative (dz*c01)*v9 rounds differently and misses
		// the original on 49 of 800 sampled calls (e.g. uid21 solve-7
		// diff[0] BD2FBAAA vs BD2FBAAC, the last 1-ULP rhs divergence).
		m_calculatedLinearDiff[0] =
			((v6*v9)*dx + dz*((A[0][1]*A[1][2] - A[0][2]*A[1][1])*v9))
			+ ((A[0][2]*A[2][1] - A[0][1]*A[2][2])*v9)*dy;
		m_calculatedLinearDiff[1] = ((v7*v9)*dx + dz*v29) + v30*dy;
		m_calculatedLinearDiff[2] = (v31*dx + dz*v32) + v12*dy;
	}
	for(int i = 0; i < 3; i++)
	{
		m_linearLimits.m_currentLinearDiff[i] = m_calculatedLinearDiff[i];
		m_linearLimits.testLimitValue(i, m_calculatedLinearDiff[i]);
	}
}



int btGeneric6DofConstraint::get_limit_motor_info2(
	btRotationalLimitMotor * limot,
	const btTransform& transA,const btTransform& transB,const btVector3& linVelA,const btVector3& linVelB,const btVector3& angVelA,const btVector3& angVelB,
	btConstraintInfo2 *info, int row, btVector3& ax1, int rotational)
{
    int srow = row * info->rowskip;
    int powered = limot->m_enableMotor;
    int limit = limot->m_currentLimit;
    // Env-gated (OPENMMD_DUMP_ROWS=<path>) per-row trace of the limit/motor
    // row inputs and the assembled rhs - used to pin the exact constraint
    // and axis behind a 1-ULP rhs divergence against the original.
    static FILE* rowLog = []() -> FILE* {
        const char* path = getenv("OPENMMD_DUMP_ROWS");
        return path ? fopen(path, "w") : nullptr;
    }();
    unsigned rowLo, rowHi, rowErr, rowErp, rowTv, rowFps, rowAx[3];
    if (rowLog) {
        memcpy(&rowLo, &limot->m_loLimit, 4); memcpy(&rowHi, &limot->m_hiLimit, 4);
        memcpy(&rowErr, &limot->m_currentLimitError, 4); memcpy(&rowErp, &limot->m_ERP, 4);
        memcpy(&rowTv, &limot->m_targetVelocity, 4); memcpy(&rowFps, &info->fps, 4);
        memcpy(&rowAx[0], &ax1[0], 4); memcpy(&rowAx[1], &ax1[1], 4); memcpy(&rowAx[2], &ax1[2], 4);
    }
    if (powered || limit)
    {   // if the joint is powered, or has joint limits, add in the extra row
        btScalar *J1 = rotational ? info->m_J1angularAxis : info->m_J1linearAxis;
        btScalar *J2 = rotational ? info->m_J2angularAxis : 0;
        J1[srow+0] = ax1[0];
        J1[srow+1] = ax1[1];
        J1[srow+2] = ax1[2];
        if(rotational)
        {
            J2[srow+0] = -ax1[0];
            J2[srow+1] = -ax1[1];
            J2[srow+2] = -ax1[2];
        }
        if((!rotational))
        {
			btVector3 ltd;	// Linear Torque Decoupling vector
			btVector3 c = m_calculatedTransformB.getOrigin() - transA.getOrigin();
			ltd = c.cross(ax1);
            info->m_J1angularAxis[srow+0] = ltd[0];
            info->m_J1angularAxis[srow+1] = ltd[1];
            info->m_J1angularAxis[srow+2] = ltd[2];

			c = m_calculatedTransformB.getOrigin() - transB.getOrigin();
			ltd = -c.cross(ax1);
			info->m_J2angularAxis[srow+0] = ltd[0];
            info->m_J2angularAxis[srow+1] = ltd[1];
            info->m_J2angularAxis[srow+2] = ltd[2];
        }
        // if we're limited low and high simultaneously, the joint motor is
        // ineffective
        if (limit && (limot->m_loLimit == limot->m_hiLimit)) powered = 0;
        info->m_constraintError[srow] = btScalar(0.f);
        if (powered)
        {
            info->cfm[srow] = 0.0f;
            if(!limit)
            {
				btScalar tag_vel = rotational ? limot->m_targetVelocity : -limot->m_targetVelocity;

				btScalar mot_fact = getMotorFactor(	limot->m_currentPosition, 
													limot->m_loLimit,
													limot->m_hiLimit, 
													tag_vel, 
													info->fps * info->erp);
				info->m_constraintError[srow] += mot_fact * limot->m_targetVelocity;
                info->m_lowerLimit[srow] = -limot->m_maxMotorForce;
                info->m_upperLimit[srow] = limot->m_maxMotorForce;
            }
        }
        if(limit)
        {
            btScalar k = info->fps * limot->m_ERP;
			if(!rotational)
			{
				info->m_constraintError[srow] += k * limot->m_currentLimitError;
			}
			else
			{
				info->m_constraintError[srow] += -k * limot->m_currentLimitError;
			}
            info->cfm[srow] = 0.0f;
            if (limot->m_loLimit == limot->m_hiLimit)
            {   // limited low and high simultaneously
                info->m_lowerLimit[srow] = -SIMD_INFINITY;
                info->m_upperLimit[srow] = SIMD_INFINITY;
            }
            else
            {
                if (limit == 1)
                {
                    info->m_lowerLimit[srow] = 0;
                    info->m_upperLimit[srow] = SIMD_INFINITY;
                }
                else
                {
                    info->m_lowerLimit[srow] = -SIMD_INFINITY;
                    info->m_upperLimit[srow] = 0;
                }
                // deal with bounce
                if (limot->m_bounce > 0)
                {
                    // calculate joint velocity.  Original 0x5021xx folds the
                    // A-side dot in (y+z)+x lanes and subtracts the B side as
                    // A - (B.x + (B.y+B.z)) - unlike the stock two separate
                    // left-associative dots this rounds differently by 1 ULP,
                    // which is exactly the first divergent solver row
                    // (rhs BDD9D669 vs BDD9D66A, solve 2, body-9 row).
                    btScalar vel;
                    if (rotational)
                    {
                        vel = (angVelA.y()*ax1[1] + angVelA.z()*ax1[2])
                            + angVelA.x()*ax1[0];
                        vel -= angVelB.x()*ax1[0]
                            + (angVelB.y()*ax1[1] + angVelB.z()*ax1[2]);
                    }
                    else
                    {
                        vel = (linVelA.y()*ax1[1] + linVelA.z()*ax1[2])
                            + linVelA.x()*ax1[0];
                        vel -= linVelB.x()*ax1[0]
                            + (linVelB.y()*ax1[1] + linVelB.z()*ax1[2]);
                    }
                    // only apply bounce if the velocity is incoming, and if the
                    // resulting c[] exceeds what we already have.
                    if (limit == 1)
                    {
                        if (vel < 0)
                        {
                            btScalar newc = -limot->m_bounce* vel;
                            if (newc > info->m_constraintError[srow]) 
								info->m_constraintError[srow] = newc;
                        }
                    }
                    else
                    {
                        if (vel > 0)
                        {
                            btScalar newc = -limot->m_bounce * vel;
                            if (newc < info->m_constraintError[srow]) 
								info->m_constraintError[srow] = newc;
                        }
                    }
                }
            }
        }
        if (rowLog) {
            unsigned rhsB, loOut, hiOut;
            memcpy(&rhsB, &info->m_constraintError[srow], 4);
            memcpy(&loOut, &info->m_lowerLimit[srow], 4);
            memcpy(&hiOut, &info->m_upperLimit[srow], 4);
            fprintf(rowLog, "row uid=%d rot=%d ax=%08X %08X %08X lo=%08X hi=%08X err=%08X erp=%08X tv=%08X fps=%08X pw=%d lim=%d rhs=%08X lo_out=%08X hi_out=%08X\n",
                    getUid(), rotational, rowAx[0], rowAx[1], rowAx[2],
                    rowLo, rowHi, rowErr, rowErp, rowTv, rowFps, powered, limit,
                    rhsB, loOut, hiOut);
            fflush(rowLog);
        }
        return 1;
    }
    else return 0;
}




