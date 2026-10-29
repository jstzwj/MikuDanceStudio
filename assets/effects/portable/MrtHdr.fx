// Original example, CC0-1.0. Bounded portable mrt-hdr-v1 scene profile.
#ifdef MDS_RHI
#define POSITION_OUT SV_POSITION
#define COLOR_OUT SV_TARGET0
#define COLOR_SECOND SV_TARGET1
cbuffer PostInputs : register(b0) { float4 multiplyColor; float4 addColor; };
Texture2D image0 : register(t0);
Texture2D image1 : register(t1);
float4 Read0(float2 uv) {
    uint w,h; image0.GetDimensions(w,h);
    return image0.Load(int3(clamp(int2(uv*float2(w,h)),int2(0,0),int2(w,h)-1),0));
}
float4 Read1(float2 uv) {
    uint w,h; image1.GetDimensions(w,h);
    return image1.Load(int3(clamp(int2(uv*float2(w,h)),int2(0,0),int2(w,h)-1),0));
}
#else
#define POSITION_OUT POSITION
#define COLOR_OUT COLOR0
#define COLOR_SECOND COLOR1
string MDSRhiProfile = "mrt-hdr-v1";
float Script : STANDARDSGLOBAL < string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color"; > = 0.8;
texture SceneColor : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture LayerA : RENDERCOLORTARGET < string Format="A16B16G16R16F"; >;
texture LayerB : RENDERCOLORTARGET < string Format="A16B16G16R16F"; >;
texture Hdr : RENDERCOLORTARGET < string Format="A16B16G16R16F"; >;
texture Tone : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture Depth : RENDERDEPTHSTENCILTARGET < string Format="D24S8"; >;
float2 viewportSize : VIEWPORTPIXELSIZE;
float4 multiplyColor = {0.5,0.75,1,0.5};
float4 addColor = {0.125,0.0625,0,0.125};
sampler SceneSampler = sampler_state {
    Texture=<SceneColor>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
sampler LayerASampler = sampler_state {
    Texture=<LayerA>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
sampler LayerBSampler = sampler_state {
    Texture=<LayerB>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
sampler HdrSampler = sampler_state {
    Texture=<Hdr>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
sampler ToneSampler = sampler_state {
    Texture=<Tone>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP; SRGBTexture=false;
};
#endif
struct Output { float4 position:POSITION_OUT; float2 uv:TEXCOORD0; };
Output fullscreen_vs(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o; o.position=float4(position,1); o.uv=uv;
#ifndef MDS_RHI
    o.uv+=0.5/viewportSize;
#endif
    return o;
}
struct Layers { float4 a:COLOR_OUT; float4 b:COLOR_SECOND; };
Layers split_ps(Output o) {
#ifdef MDS_RHI
    precise float4 c=Read0(o.uv);
#else
    float4 c=tex2D(SceneSampler,o.uv);
#endif
    Layers layers;
    layers.a=c*float4(4,2,8,1)+float4(-1.5,-.5,2,0);
    layers.b=c*float4(.5,3,.25,1)+float4(.5,-2,.25,0);
    return layers;
}
float4 combine_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    precise float4 a=Read0(o.uv), b=Read1(o.uv);
#else
    float4 a=tex2D(LayerASampler,o.uv), b=tex2D(LayerBSampler,o.uv);
#endif
    return a*multiplyColor+b+addColor;
}
float4 tone_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    precise float4 c=Read0(o.uv);
#else
    float4 c=tex2D(HdrSampler,o.uv);
#endif
    c.rgb=max(c.rgb,0); c.rgb=c.rgb/(1+c.rgb); return c;
}
float4 finish_ps(Output o):COLOR_OUT {
#ifdef MDS_RHI
    return Read0(o.uv);
#else
    return tex2D(ToneSampler,o.uv);
#endif
}
#ifndef MDS_RHI
technique Post < string Script="RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;"
    "Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=LayerA;RenderColorTarget1=LayerB;"
    "RenderDepthStencilTarget=;Pass=Split;RenderColorTarget1=;RenderColorTarget0=Hdr;Pass=Combine;"
    "RenderColorTarget0=Tone;Pass=ToneMap;RenderColorTarget0=;Pass=Finish;"; > {
    pass Split < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="split_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false; AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; ColorWriteEnable1=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 split_ps();
    }
    pass Combine < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="combine_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false; AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 combine_ps();
    }
    pass ToneMap < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="tone_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false; AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 tone_ps();
    }
    pass Finish < string Script="Draw=Buffer;"; string MDSRhiVS="fullscreen_vs"; string MDSRhiPS="finish_ps"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false; AlphaTestEnable=false; AlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 fullscreen_vs(); PixelShader=compile ps_3_0 finish_ps();
    }
}
#endif
