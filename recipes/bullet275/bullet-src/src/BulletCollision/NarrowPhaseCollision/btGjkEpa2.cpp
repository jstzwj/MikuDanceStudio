/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2008 Erwin Coumans  http://continuousphysics.com/Bullet/

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in a
product, an acknowledgment in the product documentation would be appreciated
but is not required.
2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

/*
GJK-EPA collision solver by Nathanael Presson, 2008
*/
#include "BulletCollision/CollisionShapes/btConvexInternalShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "btGjkEpa2.h"

#if defined(DEBUG) || defined (_DEBUG)
#include <stdio.h> //for debug printf
#ifdef __SPU__
#include <spu_printf.h>
#define printf spu_printf
#endif //__SPU__
#endif

namespace gjkepa2_impl
{

// VC9 (MMD's compiler) computes GJK length chains on the x87 stack and
// rounds to float exactly once; modern MSVC rounds every mulss/addss step
// and takes the square root in double precision.  The helpers below replay
// the original instruction lifetime bit for bit.
static __declspec(noinline) float mmdX87Length(const btVector3& v)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	float result;
	const float* p = &v.m_floats[0];
	__asm
	{
		mov eax, p
		fld dword ptr [eax+8]
		fld dword ptr [eax+4]
		fld dword ptr [eax]
		fmul st, st
		fld st(1)
		fmulp st(2), st
		faddp st(1), st
		fld st(1)
		fmulp st(2), st
		faddp st(1), st
		fsqrt
		fstp dword ptr [result]
	}
	return result;
#else
	return v.length();
#endif
}

// GJK::getsupport's normalize (MMD 0x4E9630): the reciprocal 1/sqrt stays
// extended on the x87 stack for the y/z components, while x multiplies the
// once-rounded float copy of that reciprocal.
static __declspec(noinline) void mmdGjkNormalizeDir(const btVector3& d,
													btVector3& out)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	float invf;
	const float* src = &d.m_floats[0];
	float* dst = &out.m_floats[0];
	__asm
	{
		mov eax, src
		mov edx, dst
		fld dword ptr [eax+8]
		fld dword ptr [eax+4]
		fld dword ptr [eax]
		fmul st, st
		fld st(1)
		fmulp st(2), st
		faddp st(1), st
		fld st(1)
		fmulp st(2), st
		faddp st(1), st
		fsqrt
		fld1
		fdivrp st(1), st
		fst dword ptr [invf]
		fld st
		fmul dword ptr [eax+4]
		movss xmm0, dword ptr [eax]
		mulss xmm0, dword ptr [invf]
		movss dword ptr [edx], xmm0
		fstp dword ptr [edx+4]
		fmul dword ptr [eax+8]
		fstp dword ptr [edx+8]
	}
	out.m_floats[3] = 0;
#else
	out = d / d.length();
#endif
}

// MMD 0x4E9760 (GJK::det): the six-term determinant is evaluated entirely
// on the x87 stack and rounded to float once.
static __declspec(noinline) float mmdX87Det(const btVector3& a,
											const btVector3& b,
											const btVector3& c)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	float result;
	const float* pa = &a.m_floats[0];
	const float* pb = &b.m_floats[0];
	const float* pc = &c.m_floats[0];
	__asm
	{
		mov eax, pa
		mov ecx, pb
		mov edx, pc
		fld dword ptr [ecx]      ; b.x
		fmul dword ptr [edx+4]   ; *c.y
		fmul dword ptr [eax+8]   ; *a.z   -> t1
		fld dword ptr [edx]      ; c.x
		fmul dword ptr [ecx+8]   ; *b.z
		fmul dword ptr [eax+4]   ; *a.y   -> t2
		faddp st(1), st
		fld dword ptr [ecx+8]    ; b.z
		fmul dword ptr [edx+4]   ; *c.y
		fmul dword ptr [eax]     ; *a.x   -> t3
		fsubp st(1), st
		fld dword ptr [eax+4]    ; a.y
		fmul dword ptr [ecx]     ; *b.x
		fmul dword ptr [edx+8]   ; *c.z   -> t4
		fsubp st(1), st
		fld dword ptr [edx+8]    ; c.z
		fmul dword ptr [ecx+4]   ; *b.y
		fmul dword ptr [eax]     ; *a.x   -> t5
		faddp st(1), st
		fld dword ptr [ecx+4]    ; b.y
		fmul dword ptr [eax+8]   ; *a.z
		fmul dword ptr [edx]     ; *c.x   -> t6
		fsubp st(1), st
		fstp dword ptr [result]
	}
	return result;
#else
	return a.y()*b.z()*c.x()+a.z()*b.x()*c.y()-
		a.x()*b.z()*c.y()-a.y()*b.x()*c.z()+
		a.x()*b.y()*c.z()-a.z()*b.y()*c.x();
#endif
}

// MMD 0x4E9950 tail: w[0]/w[1] are extended-precision products
// sqrt((z*z+y*y)+x*x) * (1/sqrt(l)), each rounded once.
static __declspec(noinline) void mmdRank3Weights(const btVector3& c1,
												 const btVector3& c2,
												 btScalar l,
												 btScalar* w)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	const float* p1 = &c1.m_floats[0];
	const float* p2 = &c2.m_floats[0];
	__asm
	{
		mov eax, p1
		mov ecx, p2
		mov edx, w
		fld dword ptr [l]
		fsqrt
		fld1
		fdivrp st(1), st        ; st0 = 1/sqrt(l), extended
		fld dword ptr [eax+8]
		fmul st, st
		fld dword ptr [eax+4]
		fmul st, st
		faddp st(1), st
		fld dword ptr [eax]
		fmul st, st
		faddp st(1), st
		fsqrt
		fmul st, st(1)          ; w[0] extended, st1 = inv
		fld st
		fstp dword ptr [edx]    ; w[0] float (keep extended below)
		fld dword ptr [ecx+8]
		fmul st, st
		fld dword ptr [ecx+4]
		fmul st, st
		faddp st(1), st
		fld dword ptr [ecx]
		fmul st, st
		faddp st(1), st
		fsqrt
		fmul st, st(2)          ; w[1] extended, st2 = inv
		fld st
		fstp dword ptr [edx+4]  ; w[1] float (keep extended below)
		faddp st(1), st         ; w1e + w0e
		fld1
		fsubrp st(1), st        ; 1 - (w1e + w0e)
		fstp dword ptr [edx+8]  ; w[2]
		fstp st(0)
	}
#else
	const btScalar s = btSqrt(l);
	w[0] = c1.length() / s;
	w[1] = c2.length() / s;
	w[2] = 1 - (w[0] + w[1]);
#endif
}

// MMD 0x4E9DF0 tail: reciprocal of vl stays extended while the three
// determinants are scaled; w[3] folds the unrounded w[2] product.
static __declspec(noinline) void mmdRank4Weights(btScalar detCBD,
												 btScalar detACD,
												 btScalar detBAD,
												 btScalar vl,
												 btScalar* w)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	__asm
	{
		mov edx, w
		fld dword ptr [vl]
		fld1
		fdivrp st(1), st        ; st0 = 1/vl extended
		fld dword ptr [detCBD]
		fmul st, st(1)          ; w[0] extended
		fld st
		fstp dword ptr [edx]    ; w[0] float
		fld dword ptr [detACD]
		fmul st, st(2)          ; w[1] extended (inv at st2)
		fld st
		fstp dword ptr [edx+4]  ; w[1] float
		fld dword ptr [detBAD]
		fmul st, st(3)          ; w[2] extended (inv at st3)
		fld st
		fstp dword ptr [edx+8]  ; w[2] float
		faddp st(1), st         ; w2e + w1e
		faddp st(1), st         ; + w0e
		fld1
		fsubrp st(1), st        ; 1 - ((w2e+w1e)+w0e)
		fstp dword ptr [edx+12] ; w[3]
		fstp st(0)
	}
#else
	const btScalar inv = btScalar(1.) / vl;
	w[0] = detCBD * inv;
	w[1] = detACD * inv;
	w[2] = detBAD * inv;
	w[3] = 1 - (w[2] + w[1] + w[0]);
#endif
}

// MMD 0x4EB420 (EPA::newface): length computed extended as
// sqrt((y*y+z*z)+x*x), rounded to float once.
static __declspec(noinline) float mmdX87LengthYXZ(const btVector3& v)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	float result;
	const float* p = &v.m_floats[0];
	__asm
	{
		mov eax, p
		fld dword ptr [eax+4]
		fmul st, st
		fld dword ptr [eax+8]
		fmul st, st
		faddp st(1), st
		fld dword ptr [eax]
		fmul st, st
		faddp st(1), st
		fsqrt
		fstp dword ptr [result]
	}
	return result;
#else
	return v.length();
#endif
}

// MMD 0x4EC670 tail: each barycentric cross length is computed extended
// as sqrt((x*x+z*z)+y*y), rounded to float once.
static __declspec(noinline) float mmdX87LengthXZY(const btVector3& v)
{
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(BT_USE_DOUBLE_PRECISION)
	float result;
	const float* p = &v.m_floats[0];
	__asm
	{
		mov eax, p
		fld dword ptr [eax]
		fmul st, st
		fld dword ptr [eax+8]
		fmul st, st
		faddp st(1), st
		fld dword ptr [eax+4]
		fmul st, st
		faddp st(1), st
		fsqrt
		fstp dword ptr [result]
	}
	return result;
#else
	return v.length();
#endif
}

	// Config

	/* GJK	*/ 
#define GJK_MAX_ITERATIONS	128
#define GJK_ACCURARY		((btScalar)0.0001)
#define GJK_MIN_DISTANCE	((btScalar)0.0001)
#define GJK_DUPLICATED_EPS	((btScalar)0.0001)
#define GJK_SIMPLEX2_EPS	((btScalar)0.0)
#define GJK_SIMPLEX3_EPS	((btScalar)0.0)
#define GJK_SIMPLEX4_EPS	((btScalar)0.0)

	/* EPA	*/ 
#define EPA_MAX_VERTICES	64
#define EPA_MAX_FACES		(EPA_MAX_VERTICES*2)
#define EPA_MAX_ITERATIONS	255
#define EPA_ACCURACY		((btScalar)0.0001)
#define EPA_FALLBACK		(10*EPA_ACCURACY)
#define EPA_PLANE_EPS		((btScalar)0.00001)
#define EPA_INSIDE_EPS		((btScalar)0.01)


	// Shorthands
	typedef unsigned int	U;
	typedef unsigned char	U1;

	// MinkowskiDiff
	struct	MinkowskiDiff
	{
		const btConvexShape*	m_shapes[2];
		btMatrix3x3				m_toshape1;
		btTransform				m_toshape0;
#ifdef __SPU__
		bool					m_enableMargin;
#else
		btVector3				(btConvexShape::*Ls)(const btVector3&) const;
#endif//__SPU__
		

		MinkowskiDiff()
		{

		}
#ifdef __SPU__
			void					EnableMargin(bool enable)
		{
			m_enableMargin = enable;
		}	
		inline btVector3		Support0(const btVector3& d) const
		{
			if (m_enableMargin)
			{
				return m_shapes[0]->localGetSupportVertexNonVirtual(d);
			} else
			{
				return m_shapes[0]->localGetSupportVertexWithoutMarginNonVirtual(d);
			}
		}
		inline btVector3		Support1(const btVector3& d) const
		{
			if (m_enableMargin)
			{
				return m_toshape0*(m_shapes[1]->localGetSupportVertexNonVirtual(m_toshape1*d));
			} else
			{
				return m_toshape0*(m_shapes[1]->localGetSupportVertexWithoutMarginNonVirtual(m_toshape1*d));
			}
		}
#else
		void					EnableMargin(bool enable)
		{
			if(enable)
				Ls=&btConvexShape::localGetSupportVertexNonVirtual;
			else
				Ls=&btConvexShape::localGetSupportVertexWithoutMarginNonVirtual;
		}	
		inline btVector3		Support0(const btVector3& d) const
		{
			return(((m_shapes[0])->*(Ls))(d));
		}
		inline btVector3		Support1(const btVector3& d) const
		{
			// VC9 (MMD 0x4E9390) evaluates every basis dot product as
			// (m[y]*y + m[z]*z) + m[x]*x with one float rounding per step,
			// and adds the origin last.  Modern MSVC folds the terms in a
			// different order, which changes GJK support points.
			const btMatrix3x3& m1 = m_toshape1;
			const btMatrix3x3& b0 = m_toshape0.getBasis();
			const btVector3& o0 = m_toshape0.getOrigin();
			const btScalar dx = d.x(), dy = d.y(), dz = d.z();
			const btVector3 local(
				(m1[0].y()*dy + m1[0].z()*dz) + m1[0].x()*dx,
				(m1[1].y()*dy + m1[1].z()*dz) + m1[1].x()*dx,
				(m1[2].y()*dy + m1[2].z()*dz) + m1[2].x()*dx);
			const btVector3 s = ((m_shapes[1])->*(Ls))(local);
			const btScalar sx = s.x(), sy = s.y(), sz = s.z();
			return btVector3(
				((b0[0].y()*sy + b0[0].z()*sz) + b0[0].x()*sx) + o0.x(),
				((b0[1].y()*sy + b0[1].z()*sz) + b0[1].x()*sx) + o0.y(),
				((b0[2].y()*sy + b0[2].z()*sz) + b0[2].x()*sx) + o0.z());
		}
#endif //__SPU__

		inline btVector3		Support(const btVector3& d) const
		{
			return(Support0(d)-Support1(-d));
		}
		btVector3				Support(const btVector3& d,U index) const
		{
			if(index)
				return(Support1(d));
			else
				return(Support0(d));
		}
	};

	typedef	MinkowskiDiff	tShape;


	// GJK
	struct	GJK
	{
		/* Types		*/ 
		struct	sSV
		{
			btVector3	d,w;
		};
		struct	sSimplex
		{
			sSV*		c[4];
			btScalar	p[4];
			U			rank;
		};
		struct	eStatus	{ enum _ {
			Valid,
			Inside,
			Failed		};};
			/* Fields		*/ 
			tShape			m_shape;
			btVector3		m_ray;
			btScalar		m_distance;
			sSimplex		m_simplices[2];
			sSV				m_store[4];
			sSV*			m_free[4];
			U				m_nfree;
			U				m_current;
			sSimplex*		m_simplex;
			eStatus::_		m_status;
			/* Methods		*/ 
			GJK()
			{
				Initialize();
			}
			void				Initialize()
			{
				m_ray		=	btVector3(0,0,0);
				m_nfree		=	0;
				m_status	=	eStatus::Failed;
				m_current	=	0;
				m_distance	=	0;
			}
			eStatus::_			Evaluate(const tShape& shapearg,const btVector3& guess)
			{
				U			iterations=0;
				btScalar	sqdist=0;
				btScalar	alpha=0;
				btVector3	lastw[4];
				U			clastw=0;
				/* Initialize solver		*/ 
				m_free[0]			=	&m_store[0];
				m_free[1]			=	&m_store[1];
				m_free[2]			=	&m_store[2];
				m_free[3]			=	&m_store[3];
				m_nfree				=	4;
				m_current			=	0;
				m_status			=	eStatus::Valid;
				m_shape				=	shapearg;
				m_distance			=	0;
				/* Initialize simplex		*/ 
				m_simplices[0].rank	=	0;
				m_ray				=	guess;
				const btScalar	sqrl=	m_ray.length2();
				appendvertice(m_simplices[0],sqrl>0?-m_ray:btVector3(1,0,0));
				m_simplices[0].p[0]	=	1;
				m_ray				=	m_simplices[0].c[0]->w;	
				sqdist				=	sqrl;
				lastw[0]			=
					lastw[1]			=
					lastw[2]			=
					lastw[3]			=	m_ray;
				/* Loop						*/ 
				do	{
					const U		next=1-m_current;
					sSimplex&	cs=m_simplices[m_current];
					sSimplex&	ns=m_simplices[next];
					/* Check zero							*/
					const btScalar	rl=mmdX87Length(m_ray);
					if(rl<GJK_MIN_DISTANCE)
					{/* Touching or inside				*/ 
						m_status=eStatus::Inside;
						break;
					}
					/* Append new vertice in -'v' direction	*/ 
					appendvertice(cs,-m_ray);
					const btVector3&	w=cs.c[cs.rank-1]->w;
					bool				found=false;
					for(U i=0;i<4;++i)
					{
						// VC9 order (MMD 0x4EA867): (dz*dz + dy*dy) + dx*dx.
						const btScalar	dzx=w.z()-lastw[i].z();
						const btScalar	dzy=w.y()-lastw[i].y();
						const btScalar	dzx2=w.x()-lastw[i].x();
						if(((dzx*dzx+dzy*dzy)+dzx2*dzx2)<GJK_DUPLICATED_EPS)
						{ found=true;break; }
					}
					if(found)
					{/* Return old simplex				*/ 
						removevertice(m_simplices[m_current]);
						break;
					}
					else
					{/* Update lastw					*/ 
						lastw[clastw=(clastw+1)&3]=w;
					}
					/* Check for termination				*/
					// VC9 order (MMD 0x4EA8BD): (rz*wz + ry*wy) + rx*wx.
					const btScalar	omega=((m_ray.z()*w.z()+m_ray.y()*w.y())
						+m_ray.x()*w.x())/rl;
					alpha=btMax(omega,alpha);
					if(((rl-alpha)-(GJK_ACCURARY*rl))<=0)
					{/* Return old simplex				*/ 
						removevertice(m_simplices[m_current]);
						break;
					}		
					/* Reduce simplex						*/ 
					btScalar	weights[4];
					U			mask=0;
					switch(cs.rank)
					{
					case	2:	sqdist=projectorigin(	cs.c[0]->w,
									cs.c[1]->w,
									weights,mask);break;
					case	3:	sqdist=projectorigin(	cs.c[0]->w,
									cs.c[1]->w,
									cs.c[2]->w,
									weights,mask);break;
					case	4:	sqdist=projectorigin(	cs.c[0]->w,
									cs.c[1]->w,
									cs.c[2]->w,
									cs.c[3]->w,
									weights,mask);break;
					}
					if(sqdist>=0)
					{/* Valid	*/ 
						ns.rank		=	0;
						m_ray		=	btVector3(0,0,0);
						m_current	=	next;
						for(U i=0,ni=cs.rank;i<ni;++i)
						{
							if(mask&(1<<i))
							{
								ns.c[ns.rank]		=	cs.c[i];
								ns.p[ns.rank++]		=	weights[i];
								m_ray				+=	cs.c[i]->w*weights[i];
							}
							else
							{
								m_free[m_nfree++]	=	cs.c[i];
							}
						}
						if(mask==15) m_status=eStatus::Inside;
					}
					else
					{/* Return old simplex				*/ 
						removevertice(m_simplices[m_current]);
						break;
					}
					m_status=((++iterations)<GJK_MAX_ITERATIONS)?m_status:eStatus::Failed;
				} while(m_status==eStatus::Valid);
				m_simplex=&m_simplices[m_current];
				switch(m_status)
				{
				case	eStatus::Valid:		m_distance=mmdX87Length(m_ray);break;
				case	eStatus::Inside:	m_distance=0;break;
				default:
					{
					}
				}	
				return(m_status);
			}
			bool					EncloseOrigin()
			{
				switch(m_simplex->rank)
				{
				case	1:
					{
						for(U i=0;i<3;++i)
						{
							btVector3		axis=btVector3(0,0,0);
							axis[i]=1;
							appendvertice(*m_simplex, axis);
							if(EncloseOrigin())	return(true);
							removevertice(*m_simplex);
							appendvertice(*m_simplex,-axis);
							if(EncloseOrigin())	return(true);
							removevertice(*m_simplex);
						}
					}
					break;
				case	2:
					{
						const btVector3	d=m_simplex->c[1]->w-m_simplex->c[0]->w;
						for(U i=0;i<3;++i)
						{
							btVector3		axis=btVector3(0,0,0);
							axis[i]=1;
							const btVector3	p=btCross(d,axis);
							if(p.length2()>0)
							{
								appendvertice(*m_simplex, p);
								if(EncloseOrigin())	return(true);
								removevertice(*m_simplex);
								appendvertice(*m_simplex,-p);
								if(EncloseOrigin())	return(true);
								removevertice(*m_simplex);
							}
						}
					}
					break;
				case	3:
					{
						const btVector3	n=btCross(m_simplex->c[1]->w-m_simplex->c[0]->w,
							m_simplex->c[2]->w-m_simplex->c[0]->w);
						if(n.length2()>0)
						{
							appendvertice(*m_simplex,n);
							if(EncloseOrigin())	return(true);
							removevertice(*m_simplex);
							appendvertice(*m_simplex,-n);
							if(EncloseOrigin())	return(true);
							removevertice(*m_simplex);
						}
					}
					break;
				case	4:
					{
						if(btFabs(mmdX87Det(m_simplex->c[0]->w-m_simplex->c[3]->w,
							m_simplex->c[1]->w-m_simplex->c[3]->w,
							m_simplex->c[2]->w-m_simplex->c[3]->w))>0)
							return(true);
					}
					break;
				}
				return(false);
			}
			/* Internals	*/ 
			void				getsupport(const btVector3& d,sSV& sv) const
			{
				mmdGjkNormalizeDir(d,sv.d);
				sv.w	=	m_shape.Support(sv.d);
			}
			void				removevertice(sSimplex& simplex)
			{
				m_free[m_nfree++]=simplex.c[--simplex.rank];
			}
			void				appendvertice(sSimplex& simplex,const btVector3& v)
			{
				simplex.p[simplex.rank]=0;
				simplex.c[simplex.rank]=m_free[--m_nfree];
				getsupport(v,*simplex.c[simplex.rank++]);
			}
			static btScalar		det(const btVector3& a,const btVector3& b,const btVector3& c)
			{
				return(	a.y()*b.z()*c.x()+a.z()*b.x()*c.y()-
					a.x()*b.z()*c.y()-a.y()*b.x()*c.z()+
					a.x()*b.y()*c.z()-a.z()*b.y()*c.x());
			}
			static btScalar		projectorigin(	const btVector3& a,
				const btVector3& b,
				btScalar* w,U& m)
			{
				// VC9 orders (MMD 0x4E97E0): length2 as (dx*dx+dz*dz)+dy*dy,
				// the dot as (az*dz+ax*dx)+ay*dy, and the returns as
				// (z*z+y*y)+x*x.
				const btScalar	dx=b.x()-a.x();
				const btScalar	dy=b.y()-a.y();
				const btScalar	dz=b.z()-a.z();
				const btScalar	l=(dx*dx+dz*dz)+dy*dy;
				if(l>GJK_SIMPLEX2_EPS)
				{
					const btScalar	t(l>0?-((a.z()*dz+a.x()*dx)+a.y()*dy)/l:0);
					if(t>=1)		{ w[0]=0;w[1]=1;m=2;
						return((b.z()*b.z()+b.y()*b.y())+b.x()*b.x()); }
					else if(t<=0)	{ w[0]=1;w[1]=0;m=1;
						return((a.z()*a.z()+a.y()*a.y())+a.x()*a.x()); }
					else
					{
						const btScalar	pz=a.z()+dz*t;
						const btScalar	px=a.x()+dx*t;
						const btScalar	py=dy*t+a.y();
						w[0]=1-(w[1]=t);m=3;
						return((pz*pz+px*px)+py*py);
					}
				}
				return(-1);
			}
			static btScalar		projectorigin(	const btVector3& a,
				const btVector3& b,
				const btVector3& c,
				btScalar* w,U& m)
			{
				static const U		imd3[]={1,2,0};
				const btVector3*	vt[]={&a,&b,&c};
				const btVector3		dl[]={a-b,b-c,c-a};
				const btVector3		n=btCross(dl[0],dl[1]);
				// VC9 orders (MMD 0x4E9950): length2 as (nz*nz+ny*ny)+nx*nx,
				// the edge dot as (z+y)+x, and the final dot as (x+y)+z.
				const btScalar		l=(n.z()*n.z()+n.y()*n.y())+n.x()*n.x();
				if(l>GJK_SIMPLEX3_EPS)
				{
					btScalar	mindist=-1;
					btScalar	subw[2]={0.f,0.f};
					U			subm(0);
					for(U i=0;i<3;++i)
					{
						const btVector3	cn=btCross(dl[i],n);
						if((vt[i]->z()*cn.z()+vt[i]->y()*cn.y())+vt[i]->x()*cn.x()>0)
						{
							const U			j=imd3[i];
							const btScalar	subd(projectorigin(*vt[i],*vt[j],subw,subm));
							if((mindist<0)||(subd<mindist))
							{
								mindist		=	subd;
								m			=	static_cast<U>(((subm&1)?1<<i:0)+((subm&2)?1<<j:0));
								w[i]		=	subw[0];
								w[j]		=	subw[1];
								w[imd3[j]]	=	0;
							}
						}
					}
					if(mindist<0)
					{
						const btScalar	d=(a.x()*n.x()+a.y()*n.y())+n.z()*a.z();
						const btScalar	px=n.x()*(d/l);
						const btScalar	py=n.y()*(d/l);
						const btScalar	pz=n.z()*(d/l);
						const btVector3	p(px,py,pz);
						mindist	=	(pz*pz+py*py)+px*px;
						m		=	7;
						mmdRank3Weights(btCross(dl[1],b-p),
							btCross(dl[2],c-p),l,w);
					}
					return(mindist);
				}
				return(-1);
			}
			static btScalar		projectorigin(	const btVector3& a,
				const btVector3& b,
				const btVector3& c,
				const btVector3& d,
				btScalar* w,U& m)
			{
				static const U		imd3[]={1,2,0};
				const btVector3*	vt[]={&a,&b,&c,&d};
				const btVector3		dl[]={a-d,b-d,c-d};
				const btScalar		vl=mmdX87Det(dl[0],dl[1],dl[2]);
				const btVector3		ngc=btCross(b-c,a-b);
				const bool			ng=(vl*((a.z()*ngc.z()+a.y()*ngc.y())+a.x()*ngc.x()))<=0;
				if(ng&&(btFabs(vl)>GJK_SIMPLEX4_EPS))
				{
					btScalar	mindist=-1;
					btScalar	subw[3]={0.f,0.f,0.f};
					U			subm(0);
					for(U i=0;i<3;++i)
					{
						const U			j=imd3[i];
						const btVector3	ej=btCross(dl[i],dl[j]);
						const btScalar	s=vl*((d.x()*ej.x()+d.y()*ej.y())
							+d.z()*ej.z());
						if(s>0)
						{
							const btScalar	subd=projectorigin(*vt[i],*vt[j],d,subw,subm);
							if((mindist<0)||(subd<mindist))
							{
								mindist		=	subd;
								m			=	static_cast<U>((subm&1?1<<i:0)+
									(subm&2?1<<j:0)+
									(subm&4?8:0));
								w[i]		=	subw[0];
								w[j]		=	subw[1];
								w[imd3[j]]	=	0;
								w[3]		=	subw[2];
							}
						}
					}
					if(mindist<0)
					{
						mindist	=	0;
						m		=	15;
						mmdRank4Weights(mmdX87Det(c,b,d),mmdX87Det(a,c,d),
							mmdX87Det(b,a,d),vl,w);
					}
					return(mindist);
				}
				return(-1);
			}
	};

	// EPA
	struct	EPA
	{
		/* Types		*/ 
		typedef	GJK::sSV	sSV;
		struct	sFace
		{
			btVector3	n;
			btScalar	d;
			btScalar	p;
			sSV*		c[3];
			sFace*		f[3];
			sFace*		l[2];
			U1			e[3];
			U1			pass;
		};
		struct	sList
		{
			sFace*		root;
			U			count;
			sList() : root(0),count(0)	{}
		};
		struct	sHorizon
		{
			sFace*		cf;
			sFace*		ff;
			U			nf;
			sHorizon() : cf(0),ff(0),nf(0)	{}
		};
		struct	eStatus { enum _ {
			Valid,
			Touching,
			Degenerated,
			NonConvex,
			InvalidHull,		
			OutOfFaces,
			OutOfVertices,
			AccuraryReached,
			FallBack,
			Failed		};};
			/* Fields		*/ 
			eStatus::_		m_status;
			GJK::sSimplex	m_result;
			btVector3		m_normal;
			btScalar		m_depth;
			sSV				m_sv_store[EPA_MAX_VERTICES];
			sFace			m_fc_store[EPA_MAX_FACES];
			U				m_nextsv;
			sList			m_hull;
			sList			m_stock;
			/* Methods		*/ 
			EPA()
			{
				Initialize();	
			}


			static inline void		bind(sFace* fa,U ea,sFace* fb,U eb)
			{
				fa->e[ea]=(U1)eb;fa->f[ea]=fb;
				fb->e[eb]=(U1)ea;fb->f[eb]=fa;
			}
			static inline void		append(sList& list,sFace* face)
			{
				face->l[0]	=	0;
				face->l[1]	=	list.root;
				if(list.root) list.root->l[0]=face;
				list.root	=	face;
				++list.count;
			}
			static inline void		remove(sList& list,sFace* face)
			{
				if(face->l[1]) face->l[1]->l[0]=face->l[0];
				if(face->l[0]) face->l[0]->l[1]=face->l[1];
				if(face==list.root) list.root=face->l[1];
				--list.count;
			}


			void				Initialize()
			{
				m_status	=	eStatus::Failed;
				m_normal	=	btVector3(0,0,0);
				m_depth		=	0;
				m_nextsv	=	0;
				for(U i=0;i<EPA_MAX_FACES;++i)
				{
					append(m_stock,&m_fc_store[EPA_MAX_FACES-i-1]);
				}
			}
			eStatus::_			Evaluate(GJK& gjk,const btVector3& guess)
			{
				GJK::sSimplex&	simplex=*gjk.m_simplex;
				if((simplex.rank>1)&&gjk.EncloseOrigin())
				{

					/* Clean up				*/ 
					while(m_hull.root)
					{
						sFace*	f = m_hull.root;
						remove(m_hull,f);
						append(m_stock,f);
					}
					m_status	=	eStatus::Valid;
					m_nextsv	=	0;
					/* Orient simplex		*/ 
					if(mmdX87Det(	simplex.c[0]->w-simplex.c[3]->w,
						simplex.c[1]->w-simplex.c[3]->w,
						simplex.c[2]->w-simplex.c[3]->w)<0)
					{
						btSwap(simplex.c[0],simplex.c[1]);
						btSwap(simplex.p[0],simplex.p[1]);
					}
					/* Build initial hull	*/ 
					sFace*	tetra[]={newface(simplex.c[0],simplex.c[1],simplex.c[2],true),
						newface(simplex.c[1],simplex.c[0],simplex.c[3],true),
						newface(simplex.c[2],simplex.c[1],simplex.c[3],true),
						newface(simplex.c[0],simplex.c[2],simplex.c[3],true)};
					if(m_hull.count==4)
					{
						sFace*		best=findbest();
						sFace		outer=*best;
						U			pass=0;
						U			iterations=0;
						bind(tetra[0],0,tetra[1],0);
						bind(tetra[0],1,tetra[2],0);
						bind(tetra[0],2,tetra[3],0);
						bind(tetra[1],1,tetra[3],2);
						bind(tetra[1],2,tetra[2],1);
						bind(tetra[2],2,tetra[3],1);
						m_status=eStatus::Valid;
						for(;iterations<EPA_MAX_ITERATIONS;++iterations)
						{
							if(m_nextsv<EPA_MAX_VERTICES)
							{	
								sHorizon		horizon;
								sSV*			w=&m_sv_store[m_nextsv++];
								bool			valid=true;					
								best->pass	=	(U1)(++pass);
								gjk.getsupport(best->n,*w);
								const btScalar	wdist=((w->w.z()*best->n.z()
								+w->w.y()*best->n.y())+w->w.x()*best->n.x())
								-best->d;
								if(wdist>EPA_ACCURACY)
								{
									for(U j=0;(j<3)&&valid;++j)
									{
										valid&=expand(	pass,w,
											best->f[j],best->e[j],
											horizon);
									}
									if(valid&&(horizon.nf>=3))
									{
										bind(horizon.cf,1,horizon.ff,2);
										remove(m_hull,best);
										append(m_stock,best);
										best=findbest();
										if(best->p>=outer.p) outer=*best;
									} else { m_status=eStatus::InvalidHull;break; }
								} else { m_status=eStatus::AccuraryReached;break; }
							} else { m_status=eStatus::OutOfVertices;break; }
						}
						const btVector3	projection=outer.n*outer.d;
						m_normal	=	outer.n;
						m_depth		=	outer.d;
						m_result.rank	=	3;
						m_result.c[0]	=	outer.c[0];
						m_result.c[1]	=	outer.c[1];
						m_result.c[2]	=	outer.c[2];
						// MMD 0x4EC670 tail: each length is an extended
						// sqrt((x*x+z*z)+y*y); the sum is ordered (p2+p1)+p0 and
						// the division is a reciprocal multiply rounded once.
						m_result.p[0]	=	mmdX87LengthXZY(btCross(
							outer.c[1]->w-projection,
							outer.c[2]->w-projection));
						m_result.p[1]	=	mmdX87LengthXZY(btCross(
							outer.c[2]->w-projection,
							outer.c[0]->w-projection));
						m_result.p[2]	=	mmdX87LengthXZY(btCross(
							outer.c[0]->w-projection,
							outer.c[1]->w-projection));
						const btScalar	sum=(m_result.p[2]+m_result.p[1])
							+m_result.p[0];
						const btScalar	inv=(float)(1.0/(double)sum);
						m_result.p[0]	=	inv*m_result.p[0];
						m_result.p[1]	=	inv*m_result.p[1];
						m_result.p[2]	=	inv*m_result.p[2];
						return(m_status);
					}
				}
				/* Fallback		*/ 
				m_status	=	eStatus::FallBack;
				m_normal	=	-guess;
				const btScalar	nl=m_normal.length();
				if(nl>0)
					m_normal	=	m_normal/nl;
				else
					m_normal	=	btVector3(1,0,0);
				m_depth	=	0;
				m_result.rank=1;
				m_result.c[0]=simplex.c[0];
				m_result.p[0]=1;	
				return(m_status);
			}
			sFace*				newface(sSV* a,sSV* b,sSV* c,bool forced)
			{
				if(m_stock.root)
				{
					sFace*	face=m_stock.root;
					remove(m_stock,face);
					append(m_hull,face);
					face->pass	=	0;
					face->c[0]	=	a;
					face->c[1]	=	b;
					face->c[2]	=	c;
					face->n		=	btCross(b->w-a->w,c->w-a->w);
					const btScalar	l=mmdX87LengthYXZ(face->n);
					const bool		v=l>EPA_ACCURACY;
					face->p		=	btMin(btMin(
						btDot(a->w,btCross(face->n,a->w-b->w)),
						btDot(b->w,btCross(face->n,b->w-c->w))),
						btDot(c->w,btCross(face->n,c->w-a->w)))	/
						(v?l:1);
					face->p		=	face->p>=-EPA_INSIDE_EPS?0:face->p;
					if(v)
					{
						// MMD 0x4EB420: dot ordered (y+z)+x, scaled by the
						// once-rounded float reciprocal 1/l (double division).
						const btScalar	inv=(float)(1.0/(double)l);
						face->d		=((a->w.y()*face->n.y()+a->w.z()*face->n.z())
							+face->n.x()*a->w.x())*inv;
						face->n		=	btVector3(face->n.x()*inv,
							face->n.y()*inv,face->n.z()*inv);
						if(forced||(face->d>=-EPA_PLANE_EPS))
						{
							return(face);
						} else m_status=eStatus::NonConvex;
					} else m_status=eStatus::Degenerated;
					remove(m_hull,face);
					append(m_stock,face);
					return(0);
				}
				m_status=m_stock.root?eStatus::OutOfVertices:eStatus::OutOfFaces;
				return(0);
			}
			sFace*				findbest()
			{
				sFace*		minf=m_hull.root;
				btScalar	mind=minf->d*minf->d;
				btScalar	maxp=minf->p;
				for(sFace* f=minf->l[1];f;f=f->l[1])
				{
					const btScalar	sqd=f->d*f->d;
					if((f->p>=maxp)&&(sqd<mind))
					{
						minf=f;
						mind=sqd;
						maxp=f->p;
					}
				}
				return(minf);
			}
			bool				expand(U pass,sSV* w,sFace* f,U e,sHorizon& horizon)
			{
				static const U	i1m3[]={1,2,0};
				static const U	i2m3[]={2,0,1};
				if(f->pass!=pass)
				{
					const U	e1=i1m3[e];
					if((btDot(f->n,w->w)-f->d)<-EPA_PLANE_EPS)
					{
						sFace*	nf=newface(f->c[e1],f->c[e],w,false);
						if(nf)
						{
							bind(nf,0,f,e);
							if(horizon.cf) bind(horizon.cf,1,nf,2); else horizon.ff=nf;
							horizon.cf=nf;
							++horizon.nf;
							return(true);
						}
					}
					else
					{
						const U	e2=i2m3[e];
						f->pass		=	(U1)pass;
						if(	expand(pass,w,f->f[e1],f->e[e1],horizon)&&
							expand(pass,w,f->f[e2],f->e[e2],horizon))
						{
							remove(m_hull,f);
							append(m_stock,f);
							return(true);
						}
					}
				}
				return(false);
			}

	};

	//
	static void	Initialize(	const btConvexShape* shape0,const btTransform& wtrs0,
		const btConvexShape* shape1,const btTransform& wtrs1,
		btGjkEpaSolver2::sResults& results,
		tShape& shape,
		bool withmargins)
	{
		/* Results		*/ 
		results.witnesses[0]	=
			results.witnesses[1]	=	btVector3(0,0,0);
		results.status			=	btGjkEpaSolver2::sResults::Separated;
		/* Shape		*/ 
		shape.m_shapes[0]		=	shape0;
		shape.m_shapes[1]		=	shape1;
		shape.m_toshape1		=	wtrs1.getBasis().transposeTimes(wtrs0.getBasis());
		shape.m_toshape0		=	wtrs0.inverseTimes(wtrs1);
		// VC9 (MMD 0x4EB9E0): the origin is computed as rot0^T * (t1 - t0)
		// with every product rounded to float separately and summed as
		// ((col_y*dy + col_z*dz) + col_x*dx); btTransform::inverseTimes folds
		// the same value differently and drifts 1 ULP.
		{
			const btMatrix3x3&	ib	=	wtrs0.getBasis();
			const btScalar		idx	=	wtrs1.getOrigin().x() - wtrs0.getOrigin().x();
			const btScalar		idy	=	wtrs1.getOrigin().y() - wtrs0.getOrigin().y();
			const btScalar		idz	=	wtrs1.getOrigin().z() - wtrs0.getOrigin().z();
			shape.m_toshape0.setOrigin(btVector3(
				(ib[1].x()*idy + ib[2].x()*idz) + ib[0].x()*idx,
				(ib[1].y()*idy + ib[2].y()*idz) + ib[0].y()*idx,
				(ib[1].z()*idy + ib[2].z()*idz) + ib[0].z()*idx));
		}
		shape.EnableMargin(withmargins);
	}

}

//
// Api
//

using namespace	gjkepa2_impl;

//
int			btGjkEpaSolver2::StackSizeRequirement()
{
	return(sizeof(GJK)+sizeof(EPA));
}

//
bool		btGjkEpaSolver2::Distance(	const btConvexShape*	shape0,
									  const btTransform&		wtrs0,
									  const btConvexShape*	shape1,
									  const btTransform&		wtrs1,
									  const btVector3&		guess,
									  sResults&				results)
{
	tShape			shape;
	Initialize(shape0,wtrs0,shape1,wtrs1,results,shape,false);
	GJK				gjk;
	GJK::eStatus::_	gjk_status=gjk.Evaluate(shape,guess);
	if(gjk_status==GJK::eStatus::Valid)
	{
		btVector3	w0=btVector3(0,0,0);
		btVector3	w1=btVector3(0,0,0);
		for(U i=0;i<gjk.m_simplex->rank;++i)
		{
			const btScalar	p=gjk.m_simplex->p[i];
			w0+=shape.Support( gjk.m_simplex->c[i]->d,0)*p;
			w1+=shape.Support(-gjk.m_simplex->c[i]->d,1)*p;
		}
		results.witnesses[0]	=	wtrs0*w0;
		results.witnesses[1]	=	wtrs0*w1;
		results.normal			=	w0-w1;
		results.distance		=	results.normal.length();
		results.normal			/=	results.distance>GJK_MIN_DISTANCE?results.distance:1;
		return(true);
	}
	else
	{
		results.status	=	gjk_status==GJK::eStatus::Inside?
			sResults::Penetrating	:
		sResults::GJK_Failed	;
		return(false);
	}
}

//
bool	btGjkEpaSolver2::Penetration(	const btConvexShape*	shape0,
									 const btTransform&		wtrs0,
									 const btConvexShape*	shape1,
									 const btTransform&		wtrs1,
									 const btVector3&		guess,
									 sResults&				results,
									 bool					usemargins)
{
	tShape			shape;
	Initialize(shape0,wtrs0,shape1,wtrs1,results,shape,usemargins);
	GJK				gjk;	
	GJK::eStatus::_	gjk_status=gjk.Evaluate(shape,-guess);
	switch(gjk_status)
	{
	case	GJK::eStatus::Inside:
		{
			EPA				epa;
			EPA::eStatus::_	epa_status=epa.Evaluate(gjk,-guess);
			if(epa_status!=EPA::eStatus::Failed)
			{
				btVector3	w0=btVector3(0,0,0);
				for(U i=0;i<epa.m_result.rank;++i)
				{
					w0+=shape.Support(epa.m_result.c[i]->d,0)*epa.m_result.p[i];
				}
				// VC9 (MMD 0x4ED0A1): the witness transforms group each basis row
				// as ((row.y*y + row.z*z) + row.x*x) + origin.
				const btMatrix3x3& wb = wtrs0.getBasis();
				const btVector3& wo = wtrs0.getOrigin();
				const btVector3 w1 = w0 - epa.m_normal*epa.m_depth;
				results.status			=	sResults::Penetrating;
				// The per-component VC9 groupings differ between the two
				// witnesses (MMD 0x4ED0A1/0x4ED180).
				results.witnesses[0]	= btVector3(
					((wb[0].z()*w0.z() + wb[0].x()*w0.x()) + wb[0].y()*w0.y()) + wo.x(),
					((wb[1].y()*w0.y() + wb[1].z()*w0.z()) + wb[1].x()*w0.x()) + wo.y(),
					((wb[2].y()*w0.y() + wb[2].z()*w0.z()) + wb[2].x()*w0.x()) + wo.z());
				results.witnesses[1]	= btVector3(
					((wb[0].z()*w1.z() + wb[0].y()*w1.y()) + wb[0].x()*w1.x()) + wo.x(),
					((wb[1].z()*w1.z() + wb[1].y()*w1.y()) + wb[1].x()*w1.x()) + wo.y(),
					((wb[2].z()*w1.z() + wb[2].y()*w1.y()) + wb[2].x()*w1.x()) + wo.z());
				results.normal			=	-epa.m_normal;
				results.distance		=	-epa.m_depth;
				return(true);
			} else results.status=sResults::EPA_Failed;
		}
		break;
	case	GJK::eStatus::Failed:
		results.status=sResults::GJK_Failed;
		break;
		default:
					{
					}
	}
	return(false);
}

#ifndef __SPU__
//
btScalar	btGjkEpaSolver2::SignedDistance(const btVector3& position,
											btScalar margin,
											const btConvexShape* shape0,
											const btTransform& wtrs0,
											sResults& results)
{
	tShape			shape;
	btSphereShape	shape1(margin);
	btTransform		wtrs1(btQuaternion(0,0,0,1),position);
	Initialize(shape0,wtrs0,&shape1,wtrs1,results,shape,false);
	GJK				gjk;	
	GJK::eStatus::_	gjk_status=gjk.Evaluate(shape,btVector3(1,1,1));
	if(gjk_status==GJK::eStatus::Valid)
	{
		btVector3	w0=btVector3(0,0,0);
		btVector3	w1=btVector3(0,0,0);
		for(U i=0;i<gjk.m_simplex->rank;++i)
		{
			const btScalar	p=gjk.m_simplex->p[i];
			w0+=shape.Support( gjk.m_simplex->c[i]->d,0)*p;
			w1+=shape.Support(-gjk.m_simplex->c[i]->d,1)*p;
		}
		results.witnesses[0]	=	wtrs0*w0;
		results.witnesses[1]	=	wtrs0*w1;
		const btVector3	delta=	results.witnesses[1]-
			results.witnesses[0];
		const btScalar	margin=	shape0->getMarginNonVirtual()+
			shape1.getMarginNonVirtual();
		const btScalar	length=	delta.length();	
		results.normal			=	delta/length;
		results.witnesses[0]	+=	results.normal*margin;
		return(length-margin);
	}
	else
	{
		if(gjk_status==GJK::eStatus::Inside)
		{
			if(Penetration(shape0,wtrs0,&shape1,wtrs1,gjk.m_ray,results))
			{
				const btVector3	delta=	results.witnesses[0]-
					results.witnesses[1];
				const btScalar	length=	delta.length();
				if (length >= SIMD_EPSILON)
					results.normal	=	delta/length;			
				return(-length);
			}
		}	
	}
	return(SIMD_INFINITY);
}

//
bool	btGjkEpaSolver2::SignedDistance(const btConvexShape*	shape0,
										const btTransform&		wtrs0,
										const btConvexShape*	shape1,
										const btTransform&		wtrs1,
										const btVector3&		guess,
										sResults&				results)
{
	if(!Distance(shape0,wtrs0,shape1,wtrs1,guess,results))
		return(Penetration(shape0,wtrs0,shape1,wtrs1,guess,results,false));
	else
		return(true);
}
#endif //__SPU__

/* Symbols cleanup		*/ 

#undef GJK_MAX_ITERATIONS
#undef GJK_ACCURARY
#undef GJK_MIN_DISTANCE
#undef GJK_DUPLICATED_EPS
#undef GJK_SIMPLEX2_EPS
#undef GJK_SIMPLEX3_EPS
#undef GJK_SIMPLEX4_EPS

#undef EPA_MAX_VERTICES
#undef EPA_MAX_FACES
#undef EPA_MAX_ITERATIONS
#undef EPA_ACCURACY
#undef EPA_FALLBACK
#undef EPA_PLANE_EPS
#undef EPA_INSIDE_EPS
