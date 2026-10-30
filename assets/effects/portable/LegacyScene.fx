// Original CC0-1.0 example. Ordinary FX9/SAS only; no RHI annotations or branch.
float Script : STANDARDSGLOBAL < string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color"; > = .8;
shared texture SharedMask : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture Captured : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture Graded : RENDERCOLORTARGET < string Format="A8R8G8B8"; >;
texture SceneDepth : RENDERDEPTHSTENCILTARGET < string Format="D24S8"; >;
float2 viewportSize : VIEWPORTPIXELSIZE;
float4 maskColor = {.5,.75,1,1};
float4 sceneClear = {1,1,1,0};
float4 gradeColor = {.5,.75,1,.5};
float4 gradeOffset = {.125,.0625,0,.125};
sampler CapturedSampler = sampler_state {
    Texture=<Captured>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler GradedSampler = sampler_state {
    Texture=<Graded>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
struct Output { float4 position:POSITION; float2 uv:TEXCOORD0; };
Output fullscreen(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o; o.position=float4(position,1); o.uv=uv+.5/viewportSize; return o;
}
// Nonlinear vertex work requires the original Buffer quad and its diagonal.
Output warped(float3 position:POSITION, float2 uv:TEXCOORD0) {
    Output o=fullscreen(position,uv); o.uv.x+=uv.x*uv.x*.03125; return o;
}
float4 grade(Output input):COLOR0 {
    return tex2D(CapturedSampler,input.uv)*gradeColor+gradeOffset;
}
float4 copy(Output input):COLOR0 { return tex2D(GradedSampler,input.uv); }
technique Scene < string Script=
    "RenderColorTarget0=SharedMask;ClearSetColor=maskColor;Clear=Color;"
    "RenderColorTarget0=Captured;RenderDepthStencilTarget=SceneDepth;"
    "ClearSetColor=sceneClear;Clear=Color;Clear=Depth;ScriptExternal=Color;"
    "RenderColorTarget0=Graded;RenderDepthStencilTarget=;Clear=Color;Pass=Grade;"
    "RenderColorTarget0=;Clear=Color;Pass=Finish;"; > {
    pass Grade < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 warped(); PixelShader=compile ps_3_0 grade();
    }
    pass Finish < string Script="Draw=Buffer;"; > {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 fullscreen(); PixelShader=compile ps_3_0 copy();
    }
}
