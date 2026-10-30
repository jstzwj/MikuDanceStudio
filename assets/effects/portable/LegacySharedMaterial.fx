// Shared-target companion, CC0-1.0.
// Original FX9 example, CC0-1.0. No RHI profile, branch or entry annotations.
float4x4 Transform : WORLDVIEWPROJECTION;
float4x4 ObjectWorld : WORLD;
float4x4 CameraView : VIEW;
float4x4 CameraProjection : PROJECTION;
float4 GeometryColor : DIFFUSE < string Object="Geometry"; >;
float2 SurfaceSize : VIEWPORTPIXELSIZE;
float4 UserTint = {0.75, 1, 0.5, 1};
shared float Exposure = 1;
bool TintEnabled = true;
float2 TintWeights[2] = {{0.5,0.25},{0.5,0.75}};
float4 LivePosition : CONTROLOBJECT < string Name="(self)"; >;
texture Albedo : MATERIALTEXTURE;
shared texture SharedMask : RENDERCOLORTARGET;
sampler2D SharedSampler = sampler_state {
    Texture=<SharedMask>; MinFilter=POINT; MagFilter=POINT; MipFilter=NONE;
    AddressU=CLAMP; AddressV=CLAMP;
};
sampler2D ColorSampler = sampler_state {
    AddressV=WRAP; Texture=<Albedo>; MinFilter=LINEAR;
    AddressU=WRAP; MipFilter=LINEAR; MagFilter=LINEAR; SRGBTexture=false;
};
struct Vertex { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0; };
struct Fragment { float4 p:POSITION; float2 uv:TEXCOORD0; float3 n:TEXCOORD1; };
Fragment VertexMain(Vertex v) {
    Fragment f;
    f.p=(mul(float4(v.p,1),Transform)+
         mul(mul(mul(float4(v.p,1),ObjectWorld),CameraView),CameraProjection))*.5;
    f.uv=v.uv;
    f.n=v.n;
    return f;
}
float4 PixelMain(Fragment f):COLOR0 {
    float4 color=(tex2D(ColorSampler,f.uv)*tex2D(SharedSampler,float2(.5,.5)))*GeometryColor;
    if (TintEnabled) color*=UserTint;
    color.rgb*=Exposure*(TintWeights[0].x+TintWeights[1].x);
    color.r+=LivePosition.x*.03125;
    // Exercise the normal input and viewport semantic alongside color/alpha.
    color.b+=f.n.x*.03125+1/SurfaceSize.x*.001;
    return color;
}
technique Main < string MmdPass="object"; bool UseTexture=true; > {
    pass Tint {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true;
        SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 VertexMain(); PixelShader=compile ps_3_0 PixelMain();
    }
}
technique Shadowed < string MmdPass="object_ss"; bool UseTexture=true; > {
    pass Tint {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true;
        SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        StencilEnable=false; ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 VertexMain(); PixelShader=compile ps_3_0 PixelMain();
    }
}
