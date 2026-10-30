// Original CC0-1.0 example. Nested DefaultEffect with an explicit LOD consumer.
texture Leaf : OFFSCREENRENDERTARGET < string Format="A8R8G8B8";
    int Width=64; int Height=32;
    int Miplevels=0; float4 ClearColor={.25,.5,.125,1}; float ClearDepth=1;
    string DefaultEffect="self=LegacyLeafMaterial.fx;*=hide;"; >;
sampler LeafSampler = sampler_state {
    Texture=<Leaf>; MinFilter=POINT; MagFilter=POINT; MipFilter=POINT;
    AddressU=CLAMP; AddressV=CLAMP;
};
float4x4 worldViewProjection : WORLDVIEWPROJECTION < string Object="Geometry"; >;
struct Output { float4 position:POSITION; float2 uv:TEXCOORD0; };
Output vertex(float4 position:POSITION, float3 normal:NORMAL, float2 uv:TEXCOORD0) {
    Output o; o.position=mul(position,worldViewProjection); o.uv=uv; return o;
}
float4 pixel(Output input):COLOR0 {
    return tex2Dlod(LeafSampler,float4(input.uv,0,1))*float4(.5,.75,1,.5);
}
technique Main < string MMDPass="object"; > {
    pass Tint {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 vertex(); PixelShader=compile ps_3_0 pixel();
    }
}
