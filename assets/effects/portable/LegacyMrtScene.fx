// Original CC0-1.0. Unannotated FX9 MRT/HDR and relative-size scene.
float Script : STANDARDSGLOBAL < string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color"; > = .8;
texture CaptureA : RENDERCOLORTARGET < string Format="A16B16G16R16F"; >;
texture CaptureB : RENDERCOLORTARGET < string Format="A16B16G16R16F"; >;
texture HalfA : RENDERCOLORTARGET < string Format="A16B16G16R16F"; float2 ViewportRatio={.5,.5}; >;
texture HalfB : RENDERCOLORTARGET < string Format="A16B16G16R16F"; float2 ViewportRatio={.5,.5}; >;
texture QuarterHdr : RENDERCOLORTARGET < string Format="A16B16G16R16F"; float2 ViewportRatio={.25,.25}; >;
texture Tone : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture SceneDepth : RENDERDEPTHSTENCILTARGET < string Format="D24S8"; >;
float2 viewportSize : VIEWPORTPIXELSIZE;
float4 clearColor = {0,0,0,0};
float clearDepth = 1;
sampler CaptureASampler = sampler_state {
    Texture=<CaptureA>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler CaptureBSampler = sampler_state {
    Texture=<CaptureB>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler HalfASampler = sampler_state {
    Texture=<HalfA>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler HalfBSampler = sampler_state {
    Texture=<HalfB>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler QuarterHdrSampler = sampler_state {
    Texture=<QuarterHdr>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler ToneSampler = sampler_state {
    Texture=<Tone>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
struct V { float4 position:POSITION; float2 uv:TEXCOORD0; };
V fullVertex(float3 position:POSITION, float2 uv:TEXCOORD0) {
    V o; o.position=float4(position,1); o.uv=uv+.5/viewportSize; return o;
}
V halfVertex(float3 position:POSITION, float2 uv:TEXCOORD0) {
    V o; o.position=float4(position,1); o.uv=uv+1/viewportSize; return o;
}
V quarterVertex(float3 position:POSITION, float2 uv:TEXCOORD0) {
    V o; o.position=float4(position,1); o.uv=uv+2/viewportSize; return o;
}
void split(V input, out float4 b:COLOR1, out float4 a:COLOR0) {
    a=tex2D(CaptureASampler,input.uv)*.5;
    b=tex2D(CaptureBSampler,input.uv)*.25;
}
float4 combine(V input):COLOR0 {
    return tex2D(HalfASampler,input.uv)+tex2D(HalfBSampler,input.uv)-float4(.5,.75,0,0);
}
float4 tone(V input):COLOR0 {
    float4 value=tex2D(QuarterHdrSampler,input.uv);
    return float4(abs(value.rgb)/(1+abs(value.rgb)),value.a);
}
float4 copy(V input):COLOR0 { return tex2D(ToneSampler,input.uv); }
technique Scene < string Script=
    "RenderColorTarget0=CaptureA;RenderColorTarget1=CaptureB;RenderDepthStencilTarget=SceneDepth;"
    "ClearSetColor=clearColor;ClearSetDepth=clearDepth;Clear=Color;Clear=Depth;ScriptExternal=Color;"
    "RenderColorTarget0=HalfA;RenderColorTarget1=HalfB;RenderDepthStencilTarget=;Clear=Color;Pass=Split;"
    "RenderColorTarget1=;RenderColorTarget0=QuarterHdr;Clear=Color;Pass=Combine;"
    "RenderColorTarget0=Tone;Clear=Color;Pass=ToneMap;"
    "RenderColorTarget0=;Clear=Color;Pass=Finish;"; > {
    pass Split < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 halfVertex(); PixelShader=compile ps_3_0 split();
    }
    pass Combine < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 quarterVertex(); PixelShader=compile ps_3_0 combine();
    }
    pass ToneMap < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 fullVertex(); PixelShader=compile ps_3_0 tone();
    }
    pass Finish < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 fullVertex(); PixelShader=compile ps_3_0 copy();
    }
}
