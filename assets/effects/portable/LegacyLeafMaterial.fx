// Original CC0-1.0 example. Public procedural leaf, no external textures.
float4x4 worldViewProjection : WORLDVIEWPROJECTION < string Object="Geometry"; >;
struct Output { float4 position:POSITION; float2 uv:TEXCOORD0; };
Output vertex(float4 position:POSITION, float3 normal:NORMAL, float2 uv:TEXCOORD0) {
    Output o; o.position=mul(position,worldViewProjection); o.uv=uv; return o;
}
float4 pixel(Output input):COLOR0 {
    return float4(.25+.5*input.uv.x,.5+.25*input.uv.y,.75,1);
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
