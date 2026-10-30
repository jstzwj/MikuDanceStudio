// Original CC0 publisher and initialization fixture for scripted MRT materials.
float Script : STANDARDSGLOBAL <string ScriptClass="scene";
    string ScriptOrder="postprocess"; string ScriptOutput="color";> = .8;
float4 Black=0;
float One=1;
shared texture Gbuffer2RT : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
shared texture Gbuffer3RT : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
shared texture Gbuffer4RT : RENDERCOLORTARGET <string Format="A16B16G16R16F";int MipLevels=1;>;
shared texture Gbuffer5RT : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
shared texture Gbuffer6RT : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
shared texture Gbuffer7RT : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
shared texture Gbuffer8RT : RENDERCOLORTARGET <string Format="A16B16G16R16F";int MipLevels=1;>;
texture Capture : RENDERCOLORTARGET <string Format="A8R8G8B8";int MipLevels=1;>;
texture Depth : RENDERDEPTHSTENCILTARGET <string Format="D24S8";>;
sampler C = sampler_state {Texture=Capture;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
sampler B = sampler_state {Texture=Gbuffer5RT;MinFilter=POINT;MagFilter=POINT;MipFilter=NONE;AddressU=CLAMP;AddressV=CLAMP;};
float4 vs(float4 p:POSITION,float2 uv:TEXCOORD0,out float2 coord:TEXCOORD0):POSITION {coord=uv;return p;}
float4 ps(float2 uv:TEXCOORD0):COLOR {return tex2D(C,uv)+tex2D(B,uv);}
technique Scene <string Script=
    "ClearSetColor=Black;ClearSetDepth=One;"
    "RenderColorTarget0=Gbuffer2RT;RenderColorTarget1=Gbuffer3RT;RenderColorTarget2=Gbuffer4RT;Clear=Color;"
    "RenderColorTarget0=Gbuffer5RT;RenderColorTarget1=Gbuffer6RT;RenderColorTarget2=Gbuffer7RT;RenderColorTarget3=Gbuffer8RT;Clear=Color;"
    "RenderColorTarget3=;RenderColorTarget2=;RenderColorTarget1=;"
    "RenderColorTarget0=Capture;RenderDepthStencilTarget=Depth;Clear=Color;Clear=Depth;ScriptExternal=Color;"
    "RenderColorTarget0=;RenderDepthStencilTarget=;Clear=Color;Pass=Combine;";> {
    pass Combine <string Script="Draw=Buffer;";> { CullMode=NONE; ZEnable=FALSE;ZWriteEnable=FALSE;
        AlphaTestEnable=FALSE;AlphaBlendEnable=FALSE;
        StencilEnable=FALSE;FillMode=SOLID;DepthBias=0;SlopeScaleDepthBias=0;
        ColorWriteEnable=15;SRGBWriteEnable=FALSE;SeparateAlphaBlendEnable=FALSE;BlendOp=ADD;
        VertexShader=compile vs_3_0 vs();PixelShader=compile ps_3_0 ps(); }
}
