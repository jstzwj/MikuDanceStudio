// Original live-parameter example, CC0-1.0. Opt-in portable post-v1 ABI.
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
cbuffer PostInputs : register(b0) { float4 multiplyColor; float4 addColor; };
Texture2D sourceImage : register(t0);
SamplerState imageSampler : register(s0);
float4 ReadSource(float2 uv) {
    uint width,height; sourceImage.GetDimensions(width,height);
    return sourceImage.Load(int3(clamp(int2(uv*float2(width,height)),int2(0,0),int2(width,height)-1),0));
}
#else
#define POSITION_OUT POSITION
#define COLOR_OUT COLOR0
float4 controlPosition : CONTROLOBJECT < string name="(self)"; string item=""; >;
float4x4 controlWorld : CONTROLOBJECT < string Name="(self)"; >;
shared float sharedExposure = 1;
float2 weights[2] = {{.5,.25},{.5,.75}};
bool enabled = true;
string MDSRhiProfile = "post-v1";
float Script : STANDARDSGLOBAL < string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color"; > = 0.8;
texture SceneColor : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture Ping : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture Depth : RENDERDEPTHSTENCILTARGET < string Format="D24S8"; >;
float2 viewportSize : VIEWPORTPIXELSIZE;
float4 multiplyColor = {0.5,0.75,1,0.5};
float4 addColor = {0.125,0.0625,0,0.125};
sampler SceneSampler = sampler_state {
    Texture=<SceneColor>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
sampler PingSampler = sampler_state {
    Texture=<Ping>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
#endif
float4 ApplyLive(float4 color) {
    float factor=sharedExposure*(weights[0].x+weights[1].x);
    color.rgb=color.rgb*factor+float3(controlPosition.x,controlWorld[3].y,0)*.03125;
    return enabled ? color : float4(0,0,0,color.a);
}
struct Output { float4 position:POSITION_OUT; float2 uv:TEXCOORD0; };
Output fullscreen_vs(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o; o.position=float4(position,1); o.uv=uv;
#ifndef MDS_RHI
    o.uv+=0.5/viewportSize; // MME's expanded DX9 Draw=Buffer quad.
#endif
    return o;
}
float4 grade_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    precise float4 color=ReadSource(o.uv);
#else
    float4 color=tex2D(SceneSampler,o.uv);
#endif
    return ApplyLive(color*multiplyColor+addColor);
}
float4 finish_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    return ReadSource(o.uv);
#else
    return tex2D(PingSampler,o.uv);
#endif
}
#ifndef MDS_RHI
technique Post < string Script="RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;"
    "Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=Ping;Pass=Grade;"
    "RenderColorTarget0=;RenderDepthStencilTarget=;Pass=Finish;"; > {
    pass Grade < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="grade_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 grade_ps();
    }
    pass Finish < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="finish_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 finish_ps();
    }
}
#endif
