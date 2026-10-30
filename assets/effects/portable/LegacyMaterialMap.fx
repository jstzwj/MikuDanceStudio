// Original CC0 fixture: ordinary FX9 material script, without portable annotations.
float4x4 wvp : WORLDVIEWPROJECTION;
texture DiffuseMap : MATERIALTEXTURE;
shared texture Gbuffer2RT : RENDERCOLORTARGET;
shared texture Gbuffer3RT : RENDERCOLORTARGET;
shared texture Gbuffer4RT : RENDERCOLORTARGET;
shared texture Gbuffer5RT : RENDERCOLORTARGET;
shared texture Gbuffer6RT : RENDERCOLORTARGET;
shared texture Gbuffer7RT : RENDERCOLORTARGET;
shared texture Gbuffer8RT : RENDERCOLORTARGET;
sampler Albedo = sampler_state {
    Texture = DiffuseMap;
    MinFilter=ANISOTROPIC; MagFilter=ANISOTROPIC; MipFilter=ANISOTROPIC;
    MaxAnisotropy=16; AddressU=WRAP; AddressV=WRAP;
};
static const float weights[2] = {.5, .25};
const float highlight=2;
struct Value { float3 rgb; float alpha; };
struct Outputs { float4 a:COLOR0; float4 b:COLOR1; float4 c:COLOR2; float4 d:COLOR3; };
float Scale(float x) { return x * weights[0]; }
float3 Scale(float3 x) { return x * weights[0]; }
// An unused library helper must not require a made-up texture/state binding.
float4 Unused(sampler image,float2 uv) { return tex2D(image,uv); }
float4 vs(float4 p:POSITION,float2 uv:TEXCOORD0,out float2 coord:TEXCOORD0):POSITION {
    coord=uv; return mul(p,wvp);
}
Outputs Shade(float2 uv) {
    float4 color=tex2D(Albedo,uv);
    Value v; v.rgb=Scale(color.rgb); v.alpha=Scale(color.a);
    Outputs o;
    o.a=float4(v.rgb,1); o.b=float4(uv,.25,1);
    o.c=float4(.5,.25,.75,1); o.d=float4(highlight+uv.x,-.5,uv.y,v.alpha);
    return o;
}
Outputs opaque(float2 uv:TEXCOORD0) { clip(.5-uv.x); return Shade(uv); }
Outputs transparent(float2 uv:TEXCOORD0) { clip(uv.x-.5); return Shade(uv); }
technique Main <string MMDPass="object"; string Script=
    "RenderColorTarget0=;RenderColorTarget1=Gbuffer2RT;RenderColorTarget2=Gbuffer3RT;RenderColorTarget3=Gbuffer4RT;Pass=Opaque;"
    "RenderColorTarget0=Gbuffer5RT;RenderColorTarget1=Gbuffer6RT;RenderColorTarget2=Gbuffer7RT;RenderColorTarget3=Gbuffer8RT;Pass=Transparent;";> {
    pass Opaque { CullMode=NONE; ZEnable=TRUE; ZWriteEnable=TRUE; ZFunc=LESSEQUAL;
        AlphaTestEnable=FALSE; AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        VertexShader=compile vs_3_0 vs(); PixelShader=compile ps_3_0 opaque(); }
    pass Transparent { CullMode=NONE; ZEnable=TRUE; ZWriteEnable=TRUE; ZFunc=LESSEQUAL;
        AlphaTestEnable=FALSE; AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;
        VertexShader=compile vs_3_0 vs(); PixelShader=compile ps_3_0 transparent(); }
}
technique Edge <string MMDPass="edge";> {}
technique Shadow <string MMDPass="shadow";> {}
technique Zplot <string MMDPass="zplot";> {}
