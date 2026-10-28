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
cbuffer Cascade : register(b0) {
    uint4 unusedStages[3];
    uint4 alphaTest;
};
float4 shadow_output(float4 color) {
    if (alphaTest.x) {
        float value = floor(saturate(color.a)*255+.5), reference = alphaTest.z;
        bool accepted = alphaTest.y == 7 ||
            (alphaTest.y == 1 && value < reference) || (alphaTest.y == 2 && value == reference) ||
            (alphaTest.y == 3 && value <= reference) || (alphaTest.y == 4 && value > reference) ||
            (alphaTest.y == 5 && value != reference) || (alphaTest.y == 6 && value >= reference);
        if (!accepted) discard;
    }
    return color;
}
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
    o.sphere = mul(float4(v.normal,0),shadowRotation).xy;
    return o;
}
ShadowPixel shadow_outline_vs(float3 position : POSITION) {
    ShadowPixel o = (ShadowPixel)0;
    o.position = mul(float4(position,1),shadowCamera);
    return o;
}
float4 shadow_ps(ShadowPixel p) : SV_TARGET {
    if (shadowOptions.x == 1) return shadow_output(float4(p.light.z/p.light.w,0,0,1));
    if (shadowOptions.x == 3) return shadow_output(shadowColor);
    float3 normal = normalize(p.normal);
    float3 specular = saturate(pow(max(0,dot(normal,normalize(-shadowDirection.xyz+normalize(p.eye)))),shadowSpecular.w)*shadowSpecular.rgb);
    float4 light = p.light/p.light.w;
    float2 uv = float2((1+light.x)*.5,(1-light.y)*.5);
    bool outside = uv.x<0 || uv.x>1 || uv.y<0 || uv.y>1;
    float comp = 1;
    if (!outside) {
        float difference = (shadowOptions.w & 65536) ? max(light.z-shadowTexture.Sample(shadowSampler,uv).r,0) : 0;
        comp = 1-saturate(shadowOptions.y ? difference*8000*uv.y-.3 : difference*1500-.3);
        comp = min(saturate(dot(normal,-shadowDirection.xyz)*3),comp);
    }
    float4 color = shadowColor;
    float alpha = color.a;
    bool baseTexture = false;
    float2 sphereUv = float2(p.sphere.x*.5+.5,p.sphere.y*-.5+.5);
    [unroll] for (uint i=0;i<3;++i) {
        uint role = (shadowOptions.w >> (i*4)) & 15;
        if (role == 1) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],p.uv);
            color *= sample; alpha *= sample.a; baseTexture = true;
        } else if (role == 2 || role == 3) {
            float4 sample = materialTextures[i].Sample(materialSamplers[i],sphereUv);
            color.rgb = role == 3 ? color.rgb+sample.rgb : color.rgb*sample.rgb;
            alpha *= sample.a;
        }
    }
    float3 result;
    if (outside || comp == 1) result = color.rgb+specular;
    else if (baseTexture) result = color.rgb*(comp+shadowToon.rgb*(1-comp))+specular*comp;
    else result = color.rgb*comp+color.rgb*shadowToon.rgb*(1-comp)+specular*comp;
    return shadow_output(float4(result,shadowOptions.z && !outside ? .5 : alpha));
}
