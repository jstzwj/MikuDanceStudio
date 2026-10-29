// Original live-parameter example, CC0-1.0. Opt-in portable material-v1 ABI.
#ifdef MDS_RHI
cbuffer LiveInputs : register(b1) {
    float4 controlPosition;
    row_major float4x4 controlWorld;
    float sharedExposure;
    float2 weights[2];
    uint enabled;
};
#define POSITION_OUT SV_POSITION
#define COLOR_OUT SV_TARGET
cbuffer MaterialInputs : register(b0) {
    row_major float4x4 worldViewProjection;
    float4 materialDiffuse;
    float4 tint;
};
Texture2D baseTexture : register(t0);
SamplerState baseSampler : register(s0);
#else
#define POSITION_OUT POSITION
#define COLOR_OUT COLOR0
float4 controlPosition : CONTROLOBJECT < string Name="(self)"; >;
float4x4 controlWorld : CONTROLOBJECT < string Name="(self)"; >;
shared float sharedExposure = 1;
float2 weights[2] = {{.5,.25},{.5,.75}};
bool enabled = true;
string MDSRhiProfile = "material-v1";
row_major float4x4 worldViewProjection : WORLDVIEWPROJECTION;
float4 materialDiffuse : DIFFUSE < string Object="Geometry"; >;
float4 tint = {0.75, 1, 0.5, 1};
float2 viewportSize : VIEWPORTPIXELSIZE;
texture baseTexture : MATERIALTEXTURE;
sampler baseSampler = sampler_state {
    Texture=<baseTexture>;
    MinFilter=LINEAR; MagFilter=LINEAR; MipFilter=LINEAR;
    AddressU=WRAP; AddressV=WRAP; SRGBTexture=false;
};
#endif
float4 ApplyLive(float4 color) {
    float factor=sharedExposure*(weights[0].x+weights[1].x);
    color.rgb=color.rgb*factor+float3(controlPosition.x,controlWorld[3].y,0)*.03125;
    return enabled ? color : float4(0,0,0,color.a);
}
struct Output { float4 position:POSITION_OUT; float2 uv:TEXCOORD0; };
Output material_vs(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o;
    o.position=mul(float4(position,1),worldViewProjection);
#ifndef MDS_RHI
    // Match the new APIs' pixel centers on the legacy DX9 material path.
    o.position.xy += float2(-1/viewportSize.x,1/viewportSize.y)*o.position.w;
#endif
    o.uv=uv; return o;
}
float4 material_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    return ApplyLive(baseTexture.Sample(baseSampler,o.uv)*materialDiffuse*tint);
#else
    return ApplyLive(tex2D(baseSampler,o.uv)*materialDiffuse*tint);
#endif
}
#ifndef MDS_RHI
technique Main < string MmdPass="object"; bool UseTexture=true; > {
    pass Tint < string MDSRhiVS="material_vs"; string MDSRhiPS="material_ps"; > {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true;
        SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 material_vs(); PixelShader=compile ps_3_0 material_ps();
    }
}
technique Shadowed < string MmdPass="object_ss"; bool UseTexture=true; > {
    pass Tint < string MDSRhiVS="material_vs"; string MDSRhiPS="material_ps"; > {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true;
        SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 material_vs(); PixelShader=compile ps_3_0 material_ps();
    }
}
#endif
