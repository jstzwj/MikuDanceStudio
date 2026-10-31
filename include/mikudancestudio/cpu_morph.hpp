#pragma once
// Shared numeric kernels extracted from ModelApplyMorphs. Storage is supplied
// by the caller; no original layout or Windows math import is required.
#include <cmath>
#include <cstdint>
namespace mikudancestudio::cpu_deform {
struct BoneMorphState {
    int boneIndex = -1;
    float translation[3]{}, rotation[4]{0, 0, 0, 1};
};
struct BoneMorphEntry {
    int boneIndex = -1;
    float translation[3]{}, rotation[4]{0, 0, 0, 1};
};
struct MaterialChannels {
    float diffuse[4]{}, specular[3]{}, specularPower = 0, ambient[3]{}, edgeColor[4]{},
                                       edgeSize = 0;
    float textureTint[4]{}, sphereTint[4]{}, toonTint[4]{};
};
struct MaterialPool {
    MaterialChannels channels;
};
struct MaterialEntry {
    std::uint8_t operation = 0;
    MaterialChannels channels;
};
// Legacy bone-morph accumulation uses the Hamilton product a*b. Preserve
// this call order independently of D3DXQuaternionMultiply's b*a convention.
inline void QuatMul(float out[4], const float a[4], const float b[4]) {
    float r[4];
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    out[3] = r[3];
}

// One bone-morph entry (0x497452 / 0x4971B7 pattern): rescale the entry
// quaternion by `w` through axis-angle and accumulate into the 32-byte work
// record selected by the entry's bone index.  `refW` folds the group-morph
// reference weight in the ORIGINAL ORDER: the x64 group path scales by TWO
// consecutive multiplies - (angle * w) * refW at 0x7FF7CB4DD9DF/0x7FF7CB4DD9EA
// and (translation * w) * refW at 0x7FF7CB4DDA9D/0x7FF7CB4DDAA4 - while the
// direct morph path multiplies by w only (0x7FF7CB4DDC49/0x7FF7CB4DDCFB);
// float multiply does not associate, so pre-combining w * refW would add a
// different rounding point.  The direct call passes refW = 1.0f (exact).
template <class Work, class Entry>
void AccumBoneEntry(Work* work, const Entry& ent, float w, float refW) {
    auto& rec = work[ent.boneIndex];
    float x = ent.rotation[0];
    float y = ent.rotation[1];
    float z = ent.rotation[2];
    float qw = ent.rotation[3];
    // x64 0x7FF7CB4DD95A / 0x7FF7CB4DDBCB: comiss clamp against 1.0f /
    // -1.0f with jbe both ways - NaN is unordered, fails both compares and
    // stays NaN.  x86 0x4971DF / 0x49747A (fld1 0x4970CD, fld -1.0f
    // flt_5295E8 0x49713B): fcom vs +1.0 with test ah,41h/jnz, then fcomp
    // vs -1.0 with test ah,5/jp - unordered sets C0/C3 (jnz takes the
    // "<= +1.0" side) and C0|C2 gives even parity (jp taken, fstp st
    // discards the constant), so NaN skips both stores and stays NaN here
    // too; the decompiler's "else w = 1.0" reading of that branch is an
    // ordered-only artifact and was audited as a false positive.
    if (qw > 1.0f)
        qw = 1.0f;
    else if (qw < -1.0f)
        qw = -1.0f;
#if defined(_M_IX86)
    double angle = std::acos(static_cast<double>(qw));
    if (qw < 0.0f)
        angle -= 3.140000104904175;
    const double len = std::sqrt(static_cast<double>(x * x + y * y + z * z));
    float q[4];
    if (angle == 0.0 || len < 0.00000011920929) {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
    } else {
        // x86 0x49729A / 0x4972A9 (group path): fmul group weight then fmul
        // ref weight, i.e. (angle * w) * refW as two x87 multiplies in that
        // order (direct path 0x49752B multiplies by w only; refW = 1.0f goes
        // through the same expression exactly).  The port used to fold
        // w * refW first, which both reorders the multiplies and adds a float
        // rounding point the original does not have.  Residual ulp-level
        // deviation kept as-is: the original stores the product back to a
        // 4-byte slot (0x4972AD / 0x49752E) before sin/cos, like its acos
        // and sqrt intermediates (0x49720E / 0x497251 / 0x49725E).
        const double scaled = (angle * w) * refW;
        const double s = std::sin(scaled) / len;
        q[0] = static_cast<float>(s * x);
        q[1] = static_cast<float>(s * y);
        q[2] = static_cast<float>(s * z);
        q[3] = static_cast<float>(std::cos(scaled));
    }
#else
    // x64 0x7FF7CB4DD979..0x7FF7CB4DDA28 (group path) and
    // 0x7FF7CB4DDBEA..0x7FF7CB4DDC87 (direct path): acosf/sqrtf/sinf/cosf
    // and a divss sin/len - single precision end to end; w<0 subtracts the
    // truncated pi 0x4048F5C3 (dword_7FF7CB552B90), the axis-length gate
    // compares FLT_EPSILON (dword_7FF7CB552B94, 0x34000000) against len.
    float angle = std::acos(qw);
    if (qw < 0.0f)
        angle -= 3.140000104904175f;
    const float len = std::sqrt(x * x + y * y + z * z);
    float q[4];
    if (angle == 0.0f || len < 0.00000011920929f) {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
    } else {
        const float scaled = (angle * w) * refW;
        const float s = std::sin(scaled) / len;
        q[0] = s * x;
        q[1] = s * y;
        q[2] = s * z;
        q[3] = std::cos(scaled);
    }
#endif
    QuatMul(rec.rotation, rec.rotation, q);
#if defined(_M_IX86)
    // x86 0x49733E..0x49736D (group) / 0x4975BB..0x49763E (direct): the
    // original accumulates each component as fld w; fmul t; fmul refW;
    // fadd work; fstp - extended-precision multiply chain and sum with a
    // single float store (direct path has one fmul; refW = 1.0f exact).
    // Keep the whole expression in double so the only rounding is the
    // final store, like the original.
    rec.translation[0] = static_cast<float>(rec.translation[0] +
                                            (w * static_cast<double>(ent.translation[0])) * refW);
    rec.translation[1] = static_cast<float>(rec.translation[1] +
                                            (w * static_cast<double>(ent.translation[1])) * refW);
    rec.translation[2] = static_cast<float>(rec.translation[2] +
                                            (w * static_cast<double>(ent.translation[2])) * refW);
#else
    rec.translation[0] += (ent.translation[0] * w) * refW;
    rec.translation[1] += (ent.translation[1] * w) * refW;
    rec.translation[2] += (ent.translation[2] * w) * refW;
#endif
}

template <std::size_t Count>
void ApplyMaterialMorphChannels(float (&add)[Count], float (&mul)[Count],
                                const float (&delta)[Count], float weight, bool additive) {
    for (std::size_t i = 0; i < Count; ++i) {
        if (additive)
            add[i] += delta[i] * weight;
        else
            mul[i] = (1.0f - (1.0f - delta[i]) * weight) * mul[i];
    }
}

inline void ApplyMaterialMorphChannels(float& add, float& mul, float delta, float weight,
                                       bool additive) {
    if (additive)
        add += delta * weight;
    else
        mul = (1.0f - (1.0f - delta) * weight) * mul;
}

// One material-morph entry applied to one named pool pair (add + multiply).
template <class Pool, class Entry>
void ApplyMatSlot(Pool& add, Pool& mul, const Entry& entry, float weight) {
    const bool additive = entry.operation != 0;
    const auto& delta = entry.channels;
    auto& addChannels = add.channels;
    auto& mulChannels = mul.channels;
    ApplyMaterialMorphChannels(addChannels.diffuse, mulChannels.diffuse, delta.diffuse, weight,
                               additive);
    ApplyMaterialMorphChannels(addChannels.specular, mulChannels.specular, delta.specular, weight,
                               additive);
    ApplyMaterialMorphChannels(addChannels.specularPower, mulChannels.specularPower,
                               delta.specularPower, weight, additive);
    ApplyMaterialMorphChannels(addChannels.ambient, mulChannels.ambient, delta.ambient, weight,
                               additive);
    ApplyMaterialMorphChannels(addChannels.edgeColor, mulChannels.edgeColor, delta.edgeColor,
                               weight, additive);
    ApplyMaterialMorphChannels(addChannels.edgeSize, mulChannels.edgeSize, delta.edgeSize, weight,
                               additive);
    ApplyMaterialMorphChannels(addChannels.textureTint, mulChannels.textureTint, delta.textureTint,
                               weight, additive);
    ApplyMaterialMorphChannels(addChannels.sphereTint, mulChannels.sphereTint, delta.sphereTint,
                               weight, additive);
    ApplyMaterialMorphChannels(addChannels.toonTint, mulChannels.toonTint, delta.toonTint, weight,
                               additive);
}

// All entries of one material morph (type 8 record), weight `w`.
template <class Pool, class Morph>
void AccumMatMorph(Pool* addP, Pool* mulP, int matCount, const Morph& morph, float w) {
    const int n = morph.materialCount;
    for (int k = 0; k < n; ++k) {
        const auto& ent = morph.materialEntries[k];
        const std::int32_t mi = ent.materialIndex;
        if (mi == -1) {
            for (int s = 0; s < matCount; ++s)
                ApplyMatSlot(addP[s], mulP[s], ent, w);
        } else {
            ApplyMatSlot(addP[mi], mulP[mi], ent, w);
        }
    }
}

}
