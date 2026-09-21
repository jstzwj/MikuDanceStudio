
/*
 * Box-Box collision detection re-distributed under the ZLib license with permission from Russell L. Smith
 * Original version is from Open Dynamics Engine, Copyright (C) 2001,2002 Russell L. Smith.
 * All rights reserved.  Email: russ@q12.org   Web: www.q12.org
 Bullet Continuous Collision Detection and Physics Library
 Bullet is Copyright (c) 2003-2006 Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

///ODE box-box collision detection is adapted to work with Bullet

#include "btBoxBoxDetector.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"

#include <float.h>
#include <string.h>

// Env-gated (OPENMMD_DUMP_DBOX=<path>) intermediate trace of the face-contact
// chain, mirroring the solver instrumentation: every value is logged as raw
// bits so offline comparison against the original's instructions is exact.
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
extern "C" int openmmdDboxTraceEnabled(void)
{
	static int state = -1;
	if (state < 0)
		state = getenv("OPENMMD_DUMP_DBOX") ? 1 : 0;
	return state;
}
extern "C" void openmmdDboxLog(const char* fmt, ...)
{
	static FILE* stream = 0;
	if (!stream)
		stream = fopen(getenv("OPENMMD_DUMP_DBOX"), "w");
	if (!stream)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(stream, fmt, args);
	va_end(args);
}
extern "C" unsigned openmmdDboxBits(float v)
{
	unsigned bits;
	memcpy(&bits, &v, 4);
	return bits;
}
#define DBX(v) openmmdDboxBits(v)

btBoxBoxDetector::btBoxBoxDetector(btBoxShape* box1,btBoxShape* box2)
: m_box1(box1),
m_box2(box2)
{

}


// given two boxes (p1,R1,side1) and (p2,R2,side2), collide them together and
// generate contact points. this returns 0 if there is no contact otherwise
// it returns the number of contacts generated.
// `normal' returns the contact normal.
// `depth' returns the maximum penetration depth along that normal.
// `return_code' returns a number indicating the type of contact that was
// detected:
//        1,2,3 = box 2 intersects with a face of box 1
//        4,5,6 = box 1 intersects with a face of box 2
//        7..15 = edge-edge contact
// `maxc' is the maximum number of contacts allowed to be generated, i.e.
// the size of the `contact' array.
// `contact' and `skip' are the contact array information provided to the
// collision functions. this function only fills in the position and depth
// fields.
struct dContactGeom;
#define dDOTpq(a,b,p,q) ((a)[0]*(b)[0] + (a)[p]*(b)[q] + (a)[2*(p)]*(b)[2*(q)])
#define dInfinity FLT_MAX


/*PURE_INLINE btScalar dDOT   (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,1,1); }
PURE_INLINE btScalar dDOT13 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,1,3); }
PURE_INLINE btScalar dDOT31 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,3,1); }
PURE_INLINE btScalar dDOT33 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,3,3); }
*/
static btScalar dDOT   (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,1,1); }
static btScalar dDOT44 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,4,4); }
static btScalar dDOT41 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,4,1); }
static btScalar dDOT14 (const btScalar *a, const btScalar *b) { return dDOTpq(a,b,1,4); }

// VC9 emits the face-contact construction as a specific chain of scalar-SSE
// operations.  A modern optimizer legally reassociates several additions even
// under the project's floating-point settings, which moves MMD contacts by a
// few ULP.  Keep these two original float rounding boundaries explicit.
#if defined(_MSC_VER)
__declspec(noinline)
#endif
static btScalar mmdAddFloat(btScalar left, btScalar right)
{
  volatile btScalar result = left + right;
  return result;
}

#if defined(_MSC_VER)
__declspec(noinline)
#endif
static btScalar mmdSubFloat(btScalar left, btScalar right)
{
  volatile btScalar result = left - right;
  return result;
}
/* VC9 (MMD): the 2x2 inverse stores 1/(m11*m22-m12*m21) through an fstps
   (reciprocal rounded to float ONCE, like the stock `det1 = 1.f/det` store)
   before scaling each component.  Pair (25,39) proved the earlier extended
   reciprocal variant wrong: its clipped contacts differ from the original by
   1-14 ULP while the float-rounded reciprocal reproduces the original
   exactly, so the plain C path below is the faithful one. */
#if 0
__declspec(noinline)
static void mmdInv22Scale(btScalar* pm11, btScalar* pm12,
                          btScalar* pm21, btScalar* pm22)
{
  const float det = (*pm11) * (*pm22) - (*pm12) * (*pm21);
  const float* pdet = &det;
  __asm
  {
    mov eax, pdet
    mov ecx, pm11
    mov edx, pm12
    fld dword ptr [eax]
    fld1
    fdivrp st(1), st      ; extended 1/det
    fld dword ptr [ecx]
    fmul st, st(1)
    fstp dword ptr [ecx]  ; m11 scaled once
    mov eax, pm21
    fld dword ptr [edx]
    fmul st, st(1)
    fstp dword ptr [edx]  ; m12
    mov edx, pm22
    fld dword ptr [eax]
    fmul st, st(1)
    fstp dword ptr [eax]  ; m21
    fld dword ptr [edx]
    fmul st, st(1)
    fstp dword ptr [edx]  ; m22
    fstp st(0)
  }
}
#endif

#define dMULTIPLYOP1_331(A,op,B,C) \
{\
  (A)[0] op dDOT41((B),(C)); \
  (A)[1] op dDOT41((B+1),(C)); \
  (A)[2] op dDOT41((B+2),(C)); \
}

#define dMULTIPLYOP0_331(A,op,B,C) \
{ \
  (A)[0] op dDOT((B),(C)); \
  (A)[1] op dDOT((B+4),(C)); \
  (A)[2] op dDOT((B+8),(C)); \
} 

#define dMULTIPLY1_331(A,B,C) dMULTIPLYOP1_331(A,=,B,C)
#define dMULTIPLY0_331(A,B,C) dMULTIPLYOP0_331(A,=,B,C)

typedef btScalar dMatrix3[4*3];

void dLineClosestApproach (const btVector3& pa, const btVector3& ua,
			   const btVector3& pb, const btVector3& ub,
			   btScalar *alpha, btScalar *beta);
void dLineClosestApproach (const btVector3& pa, const btVector3& ua,
			   const btVector3& pb, const btVector3& ub,
			   btScalar *alpha, btScalar *beta)
{
  btVector3 p;
  p[0] = pb[0] - pa[0];
  p[1] = pb[1] - pa[1];
  p[2] = pb[2] - pa[2];
  btScalar uaub = dDOT(ua,ub);
  btScalar q1 =  dDOT(ua,p);
  btScalar q2 = -dDOT(ub,p);
  btScalar d = 1-uaub*uaub;
  if (d <= btScalar(0.0001f)) {
    // @@@ this needs to be made more robust
    *alpha = 0;
    *beta  = 0;
  }
  else {
    d = 1.f/d;
    *alpha = (q1 + uaub*q2)*d;
    *beta  = (uaub*q1 + q2)*d;
  }
}



// find all the intersection points between the 2D rectangle with vertices
// at (+/-h[0],+/-h[1]) and the 2D quadrilateral with vertices (p[0],p[1]),
// (p[2],p[3]),(p[4],p[5]),(p[6],p[7]).
//
// the intersection points are returned as x,y pairs in the 'ret' array.
// the number of intersection points is returned by the function (this will
// be in the range 0 to 8).

static int intersectRectQuad2 (btScalar h[2], btScalar p[8], btScalar ret[16])
{
  // q (and r) contain nq (and nr) coordinate points for the current (and
  // chopped) polygons
  int nq=4,nr=0;
  btScalar buffer[16];
  btScalar *q = p;
  btScalar *r = ret;
  for (int dir=0; dir <= 1; dir++) {
    // direction notation: xy[0] = x axis, xy[1] = y axis
    for (int sign=-1; sign <= 1; sign += 2) {
      // chop q along the line xy[dir] = sign*h[dir]
      btScalar *pq = q;
      btScalar *pr = r;
      nr = 0;
      for (int i=nq; i > 0; i--) {
	// go through all points in q and all lines between adjacent points
	if (sign*pq[dir] < h[dir]) {
	  // this point is inside the chopping line
	  pr[0] = pq[0];
	  pr[1] = pq[1];
	  pr += 2;
	  nr++;
	  if (nr & 8) {
	    q = r;
	    goto done;
	  }
	}
	btScalar *nextq = (i > 1) ? pq+2 : q;
	if ((sign*pq[dir] < h[dir]) ^ (sign*nextq[dir] < h[dir])) {
	  // this line crosses the chopping line
	  pr[1-dir] = pq[1-dir] + (nextq[1-dir]-pq[1-dir]) /
	    (nextq[dir]-pq[dir]) * (sign*h[dir]-pq[dir]);
	  pr[dir] = sign*h[dir];
	  pr += 2;
	  nr++;
	  if (nr & 8) {
	    q = r;
	    goto done;
	  }
	}
	pq += 2;
      }
      q = r;
      r = (q==ret) ? buffer : ret;
      nq = nr;
    }
  }
 done:
  if (q != ret) memcpy (ret,q,nr*2*sizeof(btScalar));
  return nr;
}


#define M__PI 3.14159265f

// given n points in the plane (array p, of size 2*n), generate m points that
// best represent the whole set. the definition of 'best' here is not
// predetermined - the idea is to select points that give good box-box
// collision detection behavior. the chosen point indexes are returned in the
// array iret (of size m). 'i0' is always the first entry in the array.
// n must be in the range [1..8]. m must be in the range [1..n]. i0 must be
// in the range [0..n-1].

void cullPoints2 (int n, btScalar p[], int m, int i0, int iret[]);
void cullPoints2 (int n, btScalar p[], int m, int i0, int iret[])
{
  // compute the centroid of the polygon in cx,cy
  int i,j;
  btScalar a,cx,cy,q;
  if (n==1) {
    cx = p[0];
    cy = p[1];
  }
  else if (n==2) {
    cx = btScalar(0.5)*(p[0] + p[2]);
    cy = btScalar(0.5)*(p[1] + p[3]);
  }
  else {
    a = 0;
    cx = 0;
    cy = 0;
    for (i=0; i<(n-1); i++) {
      q = p[i*2]*p[i*2+3] - p[i*2+2]*p[i*2+1];
      a += q;
      cx += q*(p[i*2]+p[i*2+2]);
      cy += q*(p[i*2+1]+p[i*2+3]);
    }
    q = p[n*2-2]*p[1] - p[0]*p[n*2-1];
	if (btFabs(a+q) > SIMD_EPSILON)
	{
		a = 1.f/(btScalar(3.0)*(a+q));
	} else
	{
		a=BT_LARGE_FLOAT;
	}
    cx = a*(cx + q*(p[n*2-2]+p[0]));
    cy = a*(cy + q*(p[n*2-1]+p[1]));
  }

  // compute the angle of each point w.r.t. the centroid
  btScalar A[8];
  for (i=0; i<n; i++) A[i] = btAtan2(p[i*2+1]-cy,p[i*2]-cx);

  // search for points that have angles closest to A[i0] + i*(2*pi/m).
  int avail[8];
  for (i=0; i<n; i++) avail[i] = 1;
  avail[i0] = 0;
  iret[0] = i0;
  iret++;
  for (j=1; j<m; j++) {
    a = btScalar(j)*(2*M__PI/m) + A[i0];
    if (a > M__PI) a -= 2*M__PI;
    btScalar maxdiff=1e9,diff;

    *iret = i0;			// iret is not allowed to keep this value, but it sometimes does, when diff=#QNAN0

    for (i=0; i<n; i++) {
      if (avail[i]) {
	diff = btFabs (A[i]-a);
	if (diff > M__PI) diff = 2*M__PI - diff;
	if (diff < maxdiff) {
	  maxdiff = diff;
	  *iret = i;
	}
      }
    }
#if defined(DEBUG) || defined (_DEBUG)
    btAssert (*iret != i0);	// ensure iret got set
#endif
    avail[*iret] = 0;
    iret++;
  }
}



int dBoxBox2 (const btVector3& p1, const dMatrix3 R1,
	     const btVector3& side1, const btVector3& p2,
	     const dMatrix3 R2, const btVector3& side2,
	     btVector3& normal, btScalar *depth, int *return_code,
		 int maxc, dContactGeom * /*contact*/, int /*skip*/,btDiscreteCollisionDetectorInterface::Result& output);
int dBoxBox2 (const btVector3& p1, const dMatrix3 R1,
	     const btVector3& side1, const btVector3& p2,
	     const dMatrix3 R2, const btVector3& side2,
	     btVector3& normal, btScalar *depth, int *return_code,
		 int maxc, dContactGeom * /*contact*/, int /*skip*/,btDiscreteCollisionDetectorInterface::Result& output)
{
  const btScalar fudge_factor = btScalar(1.05);
  btVector3 p,pp,normalC(0.f,0.f,0.f);
  const btScalar *normalR = 0;
  btScalar A[3],B[3],R11,R12,R13,R21,R22,R23,R31,R32,R33,
    Q11,Q12,Q13,Q21,Q22,Q23,Q31,Q32,Q33,s,s2,l;
  int i,j,invert_normal,code;

  // get vector from centers of box 1 to box 2, relative to box 1
  p = p2 - p1;
  // MMD's VC9 build evaluates all three transpose-dot products z+y+x.
  // The source macro's x+y+z spelling is reassociated differently by VS2022.
  pp[0] = mmdAddFloat(mmdAddFloat(R1[8]*p[2],R1[4]*p[1]),R1[0]*p[0]);
  pp[1] = mmdAddFloat(mmdAddFloat(R1[9]*p[2],R1[5]*p[1]),R1[1]*p[0]);
  pp[2] = mmdAddFloat(mmdAddFloat(R1[10]*p[2],R1[6]*p[1]),R1[2]*p[0]);

  // get side lengths / 2
  A[0] = side1[0]*btScalar(0.5);
  A[1] = side1[1]*btScalar(0.5);
  A[2] = side1[2]*btScalar(0.5);
  B[0] = side2[0]*btScalar(0.5);
  B[1] = side2[1]*btScalar(0.5);
  B[2] = side2[2]*btScalar(0.5);

  // Rij is R1'*R2, i.e. the relative rotation between R1 and R2
  // Preserve the exact scalar-SSE scheduling at MMD 0x4E756A..0x4E777B.
  R11 = mmdAddFloat(mmdAddFloat(R1[0]*R2[0],R1[8]*R2[8]),R1[4]*R2[4]);
  // VC9 happens to keep this one dot product on x87 until its final store.
  R12 = (btScalar)((double)R1[8]*R2[9] +
                   (double)R1[4]*R2[5] +
                   (double)R1[0]*R2[1]);
  R13 = mmdAddFloat(mmdAddFloat(R1[4]*R2[6],R1[8]*R2[10]),R1[0]*R2[2]);
  R21 = mmdAddFloat(mmdAddFloat(R1[5]*R2[4],R1[9]*R2[8]),R1[1]*R2[0]);
  R22 = mmdAddFloat(mmdAddFloat(R1[9]*R2[9],R1[5]*R2[5]),R1[1]*R2[1]);
  R23 = mmdAddFloat(mmdAddFloat(R1[9]*R2[10],R1[5]*R2[6]),R1[1]*R2[2]);
  R31 = mmdAddFloat(mmdAddFloat(R1[6]*R2[4],R1[10]*R2[8]),R1[2]*R2[0]);
  R32 = mmdAddFloat(mmdAddFloat(R1[10]*R2[9],R1[6]*R2[5]),R1[2]*R2[1]);
  R33 = mmdAddFloat(mmdAddFloat(R1[10]*R2[10],R1[6]*R2[6]),R1[2]*R2[2]);

  Q11 = btFabs(R11); Q12 = btFabs(R12); Q13 = btFabs(R13);
  Q21 = btFabs(R21); Q22 = btFabs(R22); Q23 = btFabs(R23);
  Q31 = btFabs(R31); Q32 = btFabs(R32); Q33 = btFabs(R33);

  // for all 15 possible separating axes:
  //   * see if the axis separates the boxes. if so, return 0.
  //   * find the depth of the penetration along the separating axis (s2)
  //   * if this is the largest depth so far, record it.
  // the normal vector will be set to the separating axis with the smallest
  // depth. note: normalR is set to point to a column of R1 or R2 if that is
  // the smallest depth normal so far. otherwise normalR is 0 and normalC is
  // set to a vector relative to body 1. invert_normal is 1 if the sign of
  // the normal should be flipped.

#define TST(expr1,expr2,norm,cc) \
  { \
  const double mmd_face_expr1 = (expr1); \
  const double mmd_face_expr2 = (expr2); \
  s2 = (btScalar)(fabs(mmd_face_expr1) - mmd_face_expr2); \
  if (s2 > 0) return 0; \
  if (s2 > s) { \
    s = s2; \
    normalR = norm; \
    invert_normal = (mmd_face_expr1 < 0); \
    code = (cc); \
  } \
  }

  s = -dInfinity;
  invert_normal = 0;
  code = 0;

  // separating axis = u1,u2,u3
  TST ((double)pp[0],((double)A[0] + (double)B[0]*Q11 +
       (double)B[1]*Q12 + (double)B[2]*Q13),R1+0,1);
  TST ((double)pp[1],((double)A[1] + (double)B[0]*Q21 +
       (double)B[1]*Q22 + (double)B[2]*Q23),R1+1,2);
  TST ((double)pp[2],((double)A[2] + (double)B[0]*Q31 +
       (double)B[1]*Q32 + (double)B[2]*Q33),R1+2,3);

  // separating axis = v1,v2,v3
  TST ((double)R2[0]*p[0] + (double)R2[4]*p[1] +
       (double)R2[8]*p[2],
       ((double)A[0]*Q11 + (double)A[1]*Q21 +
        (double)A[2]*Q31 + (double)B[0]),R2+0,4);
  TST ((double)R2[1]*p[0] + (double)R2[5]*p[1] +
       (double)R2[9]*p[2],
       ((double)A[0]*Q12 + (double)A[1]*Q22 +
        (double)A[2]*Q32 + (double)B[1]),R2+1,5);
  TST ((double)R2[2]*p[0] + (double)R2[6]*p[1] +
       (double)R2[10]*p[2],
       ((double)A[0]*Q13 + (double)A[1]*Q23 +
        (double)A[2]*Q33 + (double)B[2]),R2+2,6);

  // note: cross product axes need to be scaled when s is computed.
  // normal (n1,n2,n3) is relative to box 1.
#undef TST
#define TST(expr1,expr2,n1,n2,n3,cc) \
  /* VC9 evaluates the two SAT expressions and the squared axis length on */ \
  /* x87, then spills s2 and sqrt(length2) to float.  It subsequently */ \
  /* forms one scalar-SSE reciprocal and multiplies both s2 and the three */ \
  /* normal components by it (MMD 0x4E7B22..0x4E8409).  Keeping every */ \
  /* operation as btScalar makes VS2022 round each product/add too early. */ \
  { \
  const double mmd_expr1 = (expr1); \
  const double mmd_expr2 = (expr2); \
  const double mmd_n1 = (n1); \
  const double mmd_n2 = (n2); \
  const double mmd_n3 = (n3); \
  s2 = (btScalar)(fabs(mmd_expr1) - mmd_expr2); \
  if (s2 > 0) return 0; \
  l = (btScalar)sqrt(mmd_n1*mmd_n1 + mmd_n2*mmd_n2 + \
                     mmd_n3*mmd_n3); \
  if (l > 0) { \
    const btScalar mmd_inv_l = btScalar(1.0) / l; \
    s2 *= mmd_inv_l; \
    if (s2*fudge_factor > s) { \
      s = s2; \
      normalR = 0; \
      normalC[0] = (btScalar)(n1)*mmd_inv_l; \
      normalC[1] = (btScalar)(n2)*mmd_inv_l; \
      normalC[2] = (btScalar)(n3)*mmd_inv_l; \
      invert_normal = ((expr1) < 0); \
      code = (cc); \
    } \
  } \
  }

  // separating axis = u1 x (v1,v2,v3)
  TST((double)pp[2]*R21-(double)pp[1]*R31,
      ((double)A[1]*Q31+(double)A[2]*Q21+
       (double)B[1]*Q13+(double)B[2]*Q12),0,-R31,R21,7);
  TST((double)pp[2]*R22-(double)pp[1]*R32,
      ((double)A[1]*Q32+(double)A[2]*Q22+
       (double)B[0]*Q13+(double)B[2]*Q11),0,-R32,R22,8);
  TST((double)pp[2]*R23-(double)pp[1]*R33,
      ((double)A[1]*Q33+(double)A[2]*Q23+
       (double)B[0]*Q12+(double)B[1]*Q11),0,-R33,R23,9);

  // separating axis = u2 x (v1,v2,v3)
  TST((double)pp[0]*R31-(double)pp[2]*R11,
      ((double)A[0]*Q31+(double)A[2]*Q11+
       (double)B[1]*Q23+(double)B[2]*Q22),R31,0,-R11,10);
  TST((double)pp[0]*R32-(double)pp[2]*R12,
      ((double)A[0]*Q32+(double)A[2]*Q12+
       (double)B[0]*Q23+(double)B[2]*Q21),R32,0,-R12,11);
  TST((double)pp[0]*R33-(double)pp[2]*R13,
      ((double)A[0]*Q33+(double)A[2]*Q13+
       (double)B[0]*Q22+(double)B[1]*Q21),R33,0,-R13,12);

  // separating axis = u3 x (v1,v2,v3)
  TST((double)pp[1]*R11-(double)pp[0]*R21,
      ((double)A[0]*Q21+(double)A[1]*Q11+
       (double)B[1]*Q33+(double)B[2]*Q32),-R21,R11,0,13);
  TST((double)pp[1]*R12-(double)pp[0]*R22,
      ((double)A[0]*Q22+(double)A[1]*Q12+
       (double)B[0]*Q33+(double)B[2]*Q31),-R22,R12,0,14);
  TST((double)pp[1]*R13-(double)pp[0]*R23,
      ((double)A[0]*Q23+(double)A[1]*Q13+
       (double)B[0]*Q32+(double)B[1]*Q31),-R23,R13,0,15);

#undef TST

  if (!code) return 0;

  // if we get to this point, the boxes interpenetrate. compute the normal
  // in global coordinates.
  if (normalR) {
    normal[0] = normalR[0];
    normal[1] = normalR[4];
    normal[2] = normalR[8];
  }
  else {
    dMULTIPLY0_331 (normal,R1,normalC);
  }
  if (invert_normal) {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
  *depth = -s;

  // compute contact point(s)

  if (code > 6) {
    // an edge from box 1 touches an edge from box 2.
    // find a point pa on the intersecting edge of box 1
    btVector3 pa;
    btScalar sign;
    for (i=0; i<3; i++) pa[i] = p1[i];
    for (j=0; j<3; j++) {
      /* MMD: dot = ((n.x*R[0][j] + n.z*R[2][j]) + n.y*R[1][j]); the
         grouping decides degenerate near-zero sign tests (edge contacts
         where the normal is perpendicular to an axis). */
      const btScalar dj = (normal[0]*R1[0*4+j] + normal[2]*R1[2*4+j])
                        + normal[1]*R1[1*4+j];
      sign = (dj > 0) ? btScalar(1.0) : btScalar(-1.0);
      for (i=0; i<3; i++) pa[i] += sign * A[j] * R1[i*4+j];
    }

    // find a point pb on the intersecting edge of box 2
    btVector3 pb;
    for (i=0; i<3; i++) pb[i] = p2[i];
    for (j=0; j<3; j++) {
      const btScalar dj2 = (normal[0]*R2[0*4+j] + normal[2]*R2[2*4+j])
                         + normal[1]*R2[1*4+j];
      sign = (dj2 > 0) ? btScalar(-1.0) : btScalar(1.0);
      for (i=0; i<3; i++) pb[i] += sign * B[j] * R2[i*4+j];
    }

    btScalar alpha,beta;
    btVector3 ua,ub;
    for (i=0; i<3; i++) ua[i] = R1[((code)-7)/3 + i*4];
    for (i=0; i<3; i++) ub[i] = R2[((code)-7)%3 + i*4];

    dLineClosestApproach (pa,ua,pb,ub,&alpha,&beta);
    for (i=0; i<3; i++) pa[i] += ua[i]*alpha;
    for (i=0; i<3; i++) pb[i] += ub[i]*beta;

	{
		
		//contact[0].pos[i] = btScalar(0.5)*(pa[i]+pb[i]);
		//contact[0].depth = *depth;
		btVector3 pointInWorld;

#ifdef USE_CENTER_POINT
	    for (i=0; i<3; i++) 
			pointInWorld[i] = (pa[i]+pb[i])*btScalar(0.5);
		output.addContactPoint(-normal,pointInWorld,-*depth);
#else
		output.addContactPoint(-normal,pb,-*depth);
#endif //
		*return_code = code;
	}
    return 1;
  }

  // okay, we have a face-something intersection (because the separating
  // axis is perpendicular to a face). define face 'a' to be the reference
  // face (i.e. the normal vector is perpendicular to this) and face 'b' to be
  // the incident face (the closest face of the other box).

  const btScalar *Ra,*Rb,*pa,*pb,*Sa,*Sb;
  if (code <= 3) {
    Ra = R1;
    Rb = R2;
    pa = p1;
    pb = p2;
    Sa = A;
    Sb = B;
  }
  else {
    Ra = R2;
    Rb = R1;
    pa = p2;
    pb = p1;
    Sa = B;
    Sb = A;
  }

  // nr = normal vector of reference face dotted with axes of incident box.
  // anr = absolute values of nr.
  btVector3 normal2,nr,anr;
  if (code <= 3) {
    normal2[0] = normal[0];
    normal2[1] = normal[1];
    normal2[2] = normal[2];
  }
  else {
    normal2[0] = -normal[0];
    normal2[1] = -normal[1];
    normal2[2] = -normal[2];
  }
  dMULTIPLY1_331 (nr,Rb,normal2);
  anr[0] = btFabs (nr[0]);
  anr[1] = btFabs (nr[1]);
  anr[2] = btFabs (nr[2]);
  if (openmmdDboxTraceEnabled()) {
    openmmdDboxLog("nr %08X %08X %08X n2 %08X %08X %08X\n",
                   DBX(nr[0]), DBX(nr[1]), DBX(nr[2]),
                   DBX(normal2[0]), DBX(normal2[1]), DBX(normal2[2]));
  }

  // find the largest compontent of anr: this corresponds to the normal
  // for the indident face. the other axis numbers of the indicent face
  // are stored in a1,a2.
  int lanr,a1,a2;
  if (anr[1] > anr[0]) {
    if (anr[1] > anr[2]) {
      a1 = 0;
      lanr = 1;
      a2 = 2;
    }
    else {
      a1 = 0;
      a2 = 1;
      lanr = 2;
    }
  }
  else {
    if (anr[0] > anr[2]) {
      lanr = 0;
      a1 = 1;
      a2 = 2;
    }
    else {
      a1 = 0;
      a2 = 1;
      lanr = 2;
    }
  }

  // compute center point of incident face, in reference-face coordinates
  btVector3 center;
  if (nr[lanr] < 0) {
    for (i=0; i<3; i++) center[i] = pb[i] - pa[i] + Sb[lanr] * Rb[i*4+lanr];
  }
  else {
    for (i=0; i<3; i++) center[i] = pb[i] - pa[i] - Sb[lanr] * Rb[i*4+lanr];
  }

  // find the normal and non-normal axis numbers of the reference box
  int codeN,code1,code2;
  if (code <= 3) codeN = code-1; else codeN = code-4;
  if (codeN==0) {
    code1 = 1;
    code2 = 2;
  }
  else if (codeN==1) {
    code1 = 0;
    code2 = 2;
  }
  else {
    code1 = 0;
    code2 = 1;
  }

  // find the four corners of the incident face, in reference-face coordinates
  btScalar quad[8];	// 2D coordinate of incident face (x,y pairs)
  btScalar c1,c2,m11,m12,m21,m22;
  if (openmmdDboxTraceEnabled()) {
    openmmdDboxLog("lanr %d a1 %d a2 %d center %08X %08X %08X\n",
                   lanr, a1, a2,
                   DBX(center[0]), DBX(center[1]), DBX(center[2]));
  }
  c1 = mmdAddFloat(mmdAddFloat(center[1]*Ra[4+code1],
                              center[2]*Ra[8+code1]),
                   center[0]*Ra[code1]);
  c2 = mmdAddFloat(mmdAddFloat(center[1]*Ra[4+code2],
                              center[2]*Ra[8+code2]),
                   center[0]*Ra[code2]);
  if (openmmdDboxTraceEnabled()) {
    openmmdDboxLog("c1 %08X c2 %08X\n", DBX(c1), DBX(c2));
  }
  // optimize this? - we have already computed this data above, but it is not
  // stored in an easy-to-index format. for now it's quicker just to recompute
  // the four dot products.
  m11 = mmdAddFloat(mmdAddFloat(Ra[8+code1]*Rb[8+a1],
                                Ra[4+code1]*Rb[4+a1]),
                    Ra[code1]*Rb[a1]);
  m12 = mmdAddFloat(mmdAddFloat(Ra[8+code1]*Rb[8+a2],
                                Ra[4+code1]*Rb[4+a2]),
                    Ra[code1]*Rb[a2]);
  m21 = mmdAddFloat(mmdAddFloat(Ra[8+code2]*Rb[8+a1],
                                Ra[4+code2]*Rb[4+a1]),
                    Ra[code2]*Rb[a1]);
  m22 = mmdAddFloat(mmdAddFloat(Ra[8+code2]*Rb[8+a2],
                                Ra[4+code2]*Rb[4+a2]),
                    Ra[code2]*Rb[a2]);
  if (openmmdDboxTraceEnabled()) {
    openmmdDboxLog("m %08X %08X %08X %08X\n",
                   DBX(m11), DBX(m12), DBX(m21), DBX(m22));
  }
  {
    btScalar k1 = m11*Sb[a1];
    btScalar k2 = m21*Sb[a1];
    btScalar k3 = m12*Sb[a2];
    btScalar k4 = m22*Sb[a2];
    const btScalar c1MinusK1 = mmdSubFloat(c1,k1);
    const btScalar c2MinusK2 = mmdSubFloat(c2,k2);
    quad[0] = mmdSubFloat(c1MinusK1,k3);
    quad[1] = mmdSubFloat(c2MinusK2,k4);
    quad[2] = mmdAddFloat(c1MinusK1,k3);
    quad[3] = mmdAddFloat(c2MinusK2,k4);
    quad[4] = mmdAddFloat(mmdAddFloat(k3,k1),c1);
    quad[5] = mmdAddFloat(mmdAddFloat(k4,k2),c2);
    quad[6] = mmdSubFloat(mmdAddFloat(k1,c1),k3);
    quad[7] = mmdSubFloat(mmdAddFloat(k2,c2),k4);
    if (openmmdDboxTraceEnabled()) {
      openmmdDboxLog("k %08X %08X %08X %08X quad %08X %08X %08X %08X "
                     "%08X %08X %08X %08X\n",
                     DBX(k1), DBX(k2), DBX(k3), DBX(k4),
                     DBX(quad[0]), DBX(quad[1]), DBX(quad[2]), DBX(quad[3]),
                     DBX(quad[4]), DBX(quad[5]), DBX(quad[6]), DBX(quad[7]));
    }
  }

  // find the size of the reference face
  btScalar rect[2];
  rect[0] = Sa[code1];
  rect[1] = Sa[code2];

  // intersect the incident and reference faces
  btScalar ret[16];
  int n = intersectRectQuad2 (rect,quad,ret);
  if (openmmdDboxTraceEnabled()) {
    openmmdDboxLog("rect %08X %08X n %d ret", DBX(rect[0]), DBX(rect[1]), n);
    for (int q = 0; q < 2*n; q++)
      openmmdDboxLog(" %08X", DBX(ret[q]));
    openmmdDboxLog("\n");
  }
  if (n < 1) return 0;		// this should never happen

  // convert the intersection points into reference-face coordinates,
  // and compute the contact position and depth for each point. only keep
  // those points that have a positive (penetrating) depth. delete points in
  // the 'ret' array as necessary so that 'point' and 'ret' correspond.
  btScalar point[3*8];		// penetrating contact points
  btScalar dep[8];			// depths for those points
  // MMD rounds the reciprocal to float once (fstps det1) before scaling
  // each component; see the comment above the disabled asm variant.
  btScalar det1 = 1.f/(m11*m22 - m12*m21);
  m11 *= det1;
  m12 *= det1;
  m21 *= det1;
  m22 *= det1;
  int cnum = 0;			// number of penetrating contact points found
  for (j=0; j < n; j++) {
    btScalar k1 =  m22*(ret[j*2]-c1) - m12*(ret[j*2+1]-c2);
    btScalar k2 = -m21*(ret[j*2]-c1) + m11*(ret[j*2+1]-c2);
    for (i=0; i<3; i++) point[cnum*3+i] =
			  mmdAddFloat(mmdAddFloat(k1*Rb[i*4+a1],
                                    k2*Rb[i*4+a2]),center[i]);
    const btScalar normalDotPoint = mmdAddFloat(
      mmdAddFloat(normal2[2]*point[cnum*3+2],
                  normal2[0]*point[cnum*3]),
      normal2[1]*point[cnum*3+1]);
    dep[cnum] = mmdSubFloat(Sa[codeN],normalDotPoint);
    if (openmmdDboxTraceEnabled()) {
      openmmdDboxLog("pt %d kk %08X %08X p %08X %08X %08X ndp %08X dep %08X"
                     "\n", j, DBX(k1), DBX(k2),
                     DBX(point[cnum*3]), DBX(point[cnum*3+1]),
                     DBX(point[cnum*3+2]), DBX(normalDotPoint),
                     DBX(dep[cnum]));
    }
    if (dep[cnum] >= 0) {
      ret[cnum*2] = ret[j*2];
      ret[cnum*2+1] = ret[j*2+1];
      cnum++;
    }
  }
  if (cnum < 1) return 0;	// this should never happen

  // we can't generate more contacts than we actually have
  if (maxc > cnum) maxc = cnum;
  if (maxc < 1) maxc = 1;

  if (cnum <= maxc) {
    // we have less contacts than we need, so we use them all
    for (j=0; j < cnum; j++) {

		//AddContactPoint...

		//dContactGeom *con = CONTACT(contact,skip*j);
      //for (i=0; i<3; i++) con->pos[i] = point[j*3+i] + pa[i];
      //con->depth = dep[j];

		btVector3 pointInWorld;
		for (i=0; i<3; i++) 
			pointInWorld[i] = point[j*3+i] + pa[i];
		output.addContactPoint(-normal,pointInWorld,-dep[j]);

    }
  }
  else {
    // we have more contacts than are wanted, some of them must be culled.
    // find the deepest point, it is always the first contact.
    int i1 = 0;
    btScalar maxdepth = dep[0];
    for (i=1; i<cnum; i++) {
      if (dep[i] > maxdepth) {
	maxdepth = dep[i];
	i1 = i;
      }
    }

    int iret[8];
    cullPoints2 (cnum,ret,maxc,i1,iret);

    for (j=0; j < maxc; j++) {
//      dContactGeom *con = CONTACT(contact,skip*j);
  //    for (i=0; i<3; i++) con->pos[i] = point[iret[j]*3+i] + pa[i];
    //  con->depth = dep[iret[j]];

		btVector3 posInWorld;
		for (i=0; i<3; i++) 
			posInWorld[i] = point[iret[j]*3+i] + pa[i];
		output.addContactPoint(-normal,posInWorld,-dep[iret[j]]);
    }
    cnum = maxc;
  }

  *return_code = code;
  return cnum;
}

void	btBoxBoxDetector::getClosestPoints(const ClosestPointInput& input,Result& output,class btIDebugDraw* /*debugDraw*/,bool /*swapResults*/)
{
	
	const btTransform& transformA = input.m_transformA;
	const btTransform& transformB = input.m_transformB;
	
	int skip = 0;
	dContactGeom *contact = 0;

	dMatrix3 R1;
	dMatrix3 R2;

	for (int j=0;j<3;j++)
	{
		R1[0+4*j] = transformA.getBasis()[j].x();
		R2[0+4*j] = transformB.getBasis()[j].x();

		R1[1+4*j] = transformA.getBasis()[j].y();
		R2[1+4*j] = transformB.getBasis()[j].y();


		R1[2+4*j] = transformA.getBasis()[j].z();
		R2[2+4*j] = transformB.getBasis()[j].z();

	}

	

	btVector3 normal;
	btScalar depth;
	int return_code;
	int maxc = 4;


	dBoxBox2 (transformA.getOrigin(), 
	R1,
	2.f*m_box1->getHalfExtentsWithMargin(),
	transformB.getOrigin(),
	R2, 
	2.f*m_box2->getHalfExtentsWithMargin(),
	normal, &depth, &return_code,
	maxc, contact, skip,
	output
	);

}
