// Original CC0-1.0. Ordinary FX9 MRT material; no profile or alternate branch.
float4x4 worldViewProjection : WORLDVIEWPROJECTION < string Object="Geometry"; >;
struct V { float4 position:POSITION; float2 uv:TEXCOORD0; };
V vertex(float4 position:POSITION, float3 normal:NORMAL, float2 uv:TEXCOORD0) {
    V o; o.position=mul(position,worldViewProjection); o.uv=uv; return o;
}
// Deliberately reversed declaration order: semantics define attachment order.
struct Gbuffer { float4 b:COLOR1; float4 a:COLOR0; };
Gbuffer pixel(V input) {
    Gbuffer o; o.a=float4(2+input.uv.x,-.5,.25,.5);
    o.b=float4(-1,3+input.uv.y,.5,.25); return o;
}
technique Main < string MMDPass="object"; > {
    pass Gbuffer {
        CullMode=None; ZEnable=false; ZWriteEnable=false;
        AlphaTestEnable=false; AlphaBlendEnable=false; StencilEnable=false;
        FillMode=Solid; DepthBias=0; SlopeScaleDepthBias=0;
        ColorWriteEnable=15; SRGBWriteEnable=false;
        SeparateAlphaBlendEnable=false; BlendOp=Add;
        VertexShader=compile vs_3_0 vertex(); PixelShader=compile ps_3_0 pixel();
    }
}
