// Original FX9 standard-parameter/signature example, CC0-1.0.
float4x4 Transform : WORLDVIEWPROJECTION;
float4x4 ObjectWorld : WORLD;
float4x4 CameraView : VIEW;
float4x4 CameraProjection : PROJECTION;
float4x4 WorldInverse : WORLDINVERSE;
float4x4 ViewInverse : VIEWINVERSE;
float4x4 ProjectionInverse : PROJECTIONINVERSE;
float4x4 WorldViewInverseTranspose : WORLDVIEWINVERSETRANSPOSE;
float4x4 ViewProjectionTranspose : VIEWPROJECTIONTRANSPOSE;
float4x4 TransformInverse : WORLDVIEWPROJECTIONINVERSE;
float4x4 MatrixProbe00 : WORLD;
float4x4 MatrixProbe01 : WORLDINVERSE;
float4x4 MatrixProbe02 : WORLDTRANSPOSE;
float4x4 MatrixProbe03 : WORLDINVERSETRANSPOSE;
float4x4 MatrixProbe10 : VIEW;
float4x4 MatrixProbe11 : VIEWINVERSE;
float4x4 MatrixProbe12 : VIEWTRANSPOSE;
float4x4 MatrixProbe13 : VIEWINVERSETRANSPOSE;
float4x4 MatrixProbe20 : PROJECTION;
float4x4 MatrixProbe21 : PROJECTIONINVERSE;
float4x4 MatrixProbe22 : PROJECTIONTRANSPOSE;
float4x4 MatrixProbe23 : PROJECTIONINVERSETRANSPOSE;
float4x4 MatrixProbe30 : WORLDVIEW;
float4x4 MatrixProbe31 : WORLDVIEWINVERSE;
float4x4 MatrixProbe32 : WORLDVIEWTRANSPOSE;
float4x4 MatrixProbe33 : WORLDVIEWINVERSETRANSPOSE;
float4x4 MatrixProbe40 : VIEWPROJECTION;
float4x4 MatrixProbe41 : VIEWPROJECTIONINVERSE;
float4x4 MatrixProbe42 : VIEWPROJECTIONTRANSPOSE;
float4x4 MatrixProbe43 : VIEWPROJECTIONINVERSETRANSPOSE;
float4x4 MatrixProbe50 : WORLDVIEWPROJECTION;
float4x4 MatrixProbe51 : WORLDVIEWPROJECTIONINVERSE;
float4x4 MatrixProbe52 : WORLDVIEWPROJECTIONTRANSPOSE;
float4x4 MatrixProbe53 : WORLDVIEWPROJECTIONINVERSETRANSPOSE;
float4x4 matWorld;
float4x4 matWorldViewProj;
float4 GeometryColor : DIFFUSE < string Object="Geometry"; >;
float4 Eye : POSITION < string Object="Camera"; >;
float4 Forward : DIRECTION < string Object="Camera"; >;
float4 SunPosition : POSITION < string Object="Light"; >;
float4 SunDirection : DIRECTION < string Object="Light0"; >;
float4 SunColor : DIFFUSE < string Object="Light"; >;
float4 LightDir;
float Seconds : TIME;
float Delta : ELAPSEDTIME;
float FrameSeconds : TIME2;
float FrameDelta : ELAPSEDTIME2;
bool use_texture;
bool use_toon;
int VertexCount;
int SubsetCount;
float2 SurfaceSize : VIEWPORTPIXELSIZE;
texture Albedo : MATERIALTEXTURE;
sampler2D ColorSampler = sampler_state {
    Texture=<Albedo>; MinFilter=LINEAR; MagFilter=LINEAR; MipFilter=LINEAR;
    AddressU=WRAP; AddressV=WRAP;
};
// DX9 supplies w=1 for XYZ vertices. The RHI wrapper must do the same.
float4 VertexMain(in float4 p:POSITION, in float3 n:NORMAL, in float2 uv:TEXCOORD0,
                  out float2 tex:TEXCOORD0, out float3 normal:TEXCOORD1):POSITION {
    tex=uv; normal=n;
    return (mul(p,Transform)+mul(p,matWorldViewProj))*.5;
}
void PixelMain(in float2 uv:TEXCOORD0, in float3 n:TEXCOORD1, out float4 color:COLOR0) {
    float probe=WorldInverse[0][0]+ViewInverse[3][0]+ProjectionInverse[1][1]
        +WorldViewInverseTranspose[2][1]+ViewProjectionTranspose[0][1]+TransformInverse[3][0]
        +ObjectWorld[0][0]+CameraView[3][0]+CameraProjection[1][1]+matWorld[3][0];
    probe+=MatrixProbe00[0][0]*.01+MatrixProbe00[3][0]*.02;
    probe+=MatrixProbe01[0][0]*.01+MatrixProbe01[3][0]*.02;
    probe+=MatrixProbe02[0][0]*.01+MatrixProbe02[3][0]*.02;
    probe+=MatrixProbe03[0][0]*.01+MatrixProbe03[3][0]*.02;
    probe+=MatrixProbe10[0][0]*.01+MatrixProbe10[3][0]*.02;
    probe+=MatrixProbe11[0][0]*.01+MatrixProbe11[3][0]*.02;
    probe+=MatrixProbe12[0][0]*.01+MatrixProbe12[3][0]*.02;
    probe+=MatrixProbe13[0][0]*.01+MatrixProbe13[3][0]*.02;
    probe+=MatrixProbe20[0][0]*.01+MatrixProbe20[3][0]*.02;
    probe+=MatrixProbe21[0][0]*.01+MatrixProbe21[3][0]*.02;
    probe+=MatrixProbe22[0][0]*.01+MatrixProbe22[3][0]*.02;
    probe+=MatrixProbe23[0][0]*.01+MatrixProbe23[3][0]*.02;
    probe+=MatrixProbe30[0][0]*.01+MatrixProbe30[3][0]*.02;
    probe+=MatrixProbe31[0][0]*.01+MatrixProbe31[3][0]*.02;
    probe+=MatrixProbe32[0][0]*.01+MatrixProbe32[3][0]*.02;
    probe+=MatrixProbe33[0][0]*.01+MatrixProbe33[3][0]*.02;
    probe+=MatrixProbe40[0][0]*.01+MatrixProbe40[3][0]*.02;
    probe+=MatrixProbe41[0][0]*.01+MatrixProbe41[3][0]*.02;
    probe+=MatrixProbe42[0][0]*.01+MatrixProbe42[3][0]*.02;
    probe+=MatrixProbe43[0][0]*.01+MatrixProbe43[3][0]*.02;
    probe+=MatrixProbe50[0][0]*.01+MatrixProbe50[3][0]*.02;
    probe+=MatrixProbe51[0][0]*.01+MatrixProbe51[3][0]*.02;
    probe+=MatrixProbe52[0][0]*.01+MatrixProbe52[3][0]*.02;
    probe+=MatrixProbe53[0][0]*.01+MatrixProbe53[3][0]*.02;
    probe+=dot(Eye+Forward+SunPosition+SunDirection+LightDir,float4(.1,.2,.3,.4));
    probe+=Seconds+Delta+FrameSeconds+FrameDelta+VertexCount*.01+SubsetCount*.02;
    color=tex2D(ColorSampler,uv)*GeometryColor;
    color.rgb*=.5+.125*SunColor.rgb;
    color.rgb+=probe*.001+n*.001+1/SurfaceSize.x*.001;
    if (!use_texture || use_toon) color.rgb*=.75;
}
technique Main < string MmdPass="object"; bool UseTexture=true; > {
    pass Tint {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false; FillMode=Solid;
        DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 VertexMain(); PixelShader=compile ps_3_0 PixelMain();
    }
}
technique Shadowed < string MmdPass="object_ss"; bool UseTexture=true; > {
    pass Tint {
        CullMode=None; ZEnable=true; ZWriteEnable=true; ZFunc=LessEqual;
        AlphaTestEnable=false; AlphaBlendEnable=true; SrcBlend=SrcAlpha; DestBlend=InvSrcAlpha;
        BlendOp=Add; SeparateAlphaBlendEnable=false; FillMode=Solid;
        DepthBias=0; SlopeScaleDepthBias=0; StencilEnable=false;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        VertexShader=compile vs_3_0 VertexMain(); PixelShader=compile ps_3_0 PixelMain();
    }
}
