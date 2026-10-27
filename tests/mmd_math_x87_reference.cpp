// Test-only historical oracle copied from d7425a5. Never link into the app.
// sinTimes/cosine: src/app/frame_modes_bone.cpp OriginalSinTimes/OriginalCos.
// integrateRotation: LinearMath/btTransformUtil.h mmdSinCosAxisAsm.
// x87 FSIN/FCOS are CPU approximations: directed-rounding output bits need not
// match another processor. Run the old instructions on the same CPU instead of
// replacing their captured golden values with values from the implementation.
// Keep these bodies frozen; do not rewrite them using btMmdMath.
#include "mmd_math_x87_reference.hpp"
#include <cmath>

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

namespace mmd_math_x87_reference {

__declspec(noinline)
float sinTimes(float angle, float factor) {
#if defined(_M_IX86)
    float sine;
    float result;
    __asm {
        fld angle
        fsin
        fstp sine
        fld sine
        fmul factor
        fstp result
    }
    return result;
#else
    return static_cast<float>(std::sin(angle) * factor);
#endif
}

__declspec(noinline)
float cosine(float angle) {
#if defined(_M_IX86)
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
#else
    return static_cast<float>(std::cos(angle));
#endif
}

__declspec(noinline)
void integrateRotation(const float* w, float dt,
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

// Frozen outline instruction oracles. MMD x86 VA 4AA95E..4AA99C and
// 4B1FEE..4B2020; these instructions must remain independent of btMmdMath.
__declspec(noinline) float pmxEdgeComponent(float position, float normal, float frameScale,
                                            float vertexScale, float materialSize) {
    float result;
    __asm {
        fld normal
        fmul frameScale
        fmul vertexScale
        fmul materialSize
        fadd position
        fstp result
    }
    return result;
}

__declspec(noinline) float edgeDistanceScale(float distance, float fov, float modelScale) {
    // Original doubles at VA 5311D0 / 5311C8, read directly from the binary.
    // The former decimal distance literal rounded one double ULP too high.
    const double distanceScale = 0x1.797cc40000000p-15;
    const double fovScale = 0x1.3333340000000p-1;
    float scale;
    __asm {
        fld distance
        fmul distanceScale
        fld fov
        fmul fovScale
        fld1
        faddp st(1), st
        fmulp st(1), st
        fstp scale
        fld scale
        fmul modelScale
        fstp scale
    }
    return scale;
}

} // namespace mmd_math_x87_reference
