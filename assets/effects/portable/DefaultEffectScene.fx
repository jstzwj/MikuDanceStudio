// Original CC0-1.0 example. Two DefaultEffect generations finish before main capture.
float Script : STANDARDSGLOBAL < string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color"; > = .8;
texture Child : OFFSCREENRENDERTARGET < string Format="A8R8G8B8";
    int Miplevels=0; float4 ClearColor={.125,.25,.5,1}; float ClearDepth=1;
    string DefaultEffect="self=hide;*.pm*=LegacyChildMaterial.fx;*=hide;"; >;
texture Captured : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture SceneDepth : RENDERDEPTHSTENCILTARGET < string Format="D24S8"; >;
float2 viewportSize : VIEWPORTPIXELSIZE;
float clearDepth = 1;
float4 clearColor = {1,1,1,0};
sampler ChildSampler = sampler_state {
    Texture=<Child>; MinFilter=POINT; MagFilter=POINT; MipFilter=POINT;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler CapturedSampler = sampler_state {
    Texture=<Captured>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
struct Output { float4 position:POSITION; float2 uv:TEXCOORD0; };
Output fullscreen(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o; o.position=float4(position,1); o.uv=uv+.5/viewportSize; return o;
}
float4 combine(Output input):COLOR0 {
    return .5*tex2D(CapturedSampler,input.uv)+.5*tex2Dlod(ChildSampler,float4(input.uv,0,2));
}
technique Scene < string Script=
    "RenderColorTarget0=Captured;RenderDepthStencilTarget=SceneDepth;"
    "ClearSetColor=clearColor;ClearSetDepth=clearDepth;Clear=Color;Clear=Depth;ScriptExternal=Color;"
    "RenderColorTarget0=;RenderDepthStencilTarget=;Clear=Color;Pass=Combine;"; > {
    pass Combine < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 fullscreen(); PixelShader=compile ps_3_0 combine();
    }
}
