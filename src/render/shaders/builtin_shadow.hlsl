// Behavioral subset of skin_effect_sm3.fx: ZValuePlot and BufferShadow /
// BShadowTexture / BShadowSphia(Texture). Material morph channels and UV1 need
// their own CPU contracts and must not be silently approximated.
cbuffer BuiltinShadow : register(b2) {
    row_major float4x4 shadowLight, shadowCamera, shadowRotation;
    float4 shadowColor, shadowToon, shadowSpecular, shadowDirection, shadowEye;
    uint4 shadowOptions; // mode, perspective, transparent, material stage roles
    float4 shadowDimensions;
};
Texture2D materialTextures[3] : register(t0);
SamplerState materialSamplers[3] : register(s0);
Texture2D shadowTexture : register(t3);
SamplerState shadowSampler : register(s3);
struct ShadowVertex {
    float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0;
};
struct ShadowPixel {
    float4 position : SV_POSITION;
    float4 light : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 eye : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float2 sphere : TEXCOORD4;
};
ShadowPixel shadow_vs(ShadowVertex v) {
    ShadowPixel o;
    o.light = mul(float4(v.position,1),shadowLight);
    o.position = shadowOptions.x == 1 ? o.light : mul(float4(v.position,1),shadowCamera);
    o.normal = v.normal; o.eye = shadowEye.xyz-v.position;
    o.uv = v.uv;
    float3 rotated = normalize(mul(float4(v.normal,0),shadowRotation).xyz);
    o.sphere = float2(rotated.x*.5+.5,rotated.y*-.5+.5);
    return o;
}
float4 shadow_ps(ShadowPixel p) : SV_TARGET {
    if (shadowOptions.x == 1) return float4(p.light.z/p.light.w,0,0,1);
    float3 normal = normalize(p.normal);
    float3 specular = saturate(pow(max(0,dot(normal,normalize(-shadowDirection.xyz+normalize(p.eye)))),shadowSpecular.w)*shadowSpecular.rgb);
    float4 light = p.light/p.light.w;
    float2 uv = float2((1+light.x)*.5,(1-light.y)*.5);
    bool outside = uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1;
    float comp = 1;
    if (!outside) {
        float difference = max(light.z-shadowTexture.Sample(shadowSampler,uv).r,0);
        comp = 1-saturate(shadowOptions.y ? difference*8000*uv.y-.3 : difference*1500-.3);
        comp = min(saturate(dot(normal,-shadowDirection.xyz)*3),comp);
    }
    float4 color = shadowColor;
    float alpha = color.a;
    [unroll] for (uint i=0;i<3;++i) {
        uint role = (shadowOptions.w >> (i*4)) & 15;
        if (role == 1) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],p.uv);
            color *= sample; alpha *= sample.a;
        } else if (role == 2 || role == 3) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],p.sphere);
            color.rgb = role == 3 ? color.rgb+sample.rgb : color.rgb*sample.rgb;
        }
    }
    float3 result = outside ? color.rgb+specular :
        color.rgb*comp + color.rgb*shadowToon.rgb*(1-comp) + specular*comp;
    return float4(result,shadowOptions.z && !outside ? .5 : alpha);
}
