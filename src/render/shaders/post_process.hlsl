Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
cbuffer PostProcess : register(b2) {
    float4 multiplyColor;
    float4 addColor;
    float4 sourceRect;
    uint4 options;
};
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Output post_vs(float3 position : POSITION, float2 uv : TEXCOORD0) {
    Output o; o.position=float4(position,1); o.uv=uv; return o;
}
float4 post_ps(Output input) : SV_TARGET {
    precise float2 uv=input.uv*sourceRect.xy+sourceRect.zw;
    uint width,height; image.GetDimensions(width,height);
    // Point loads preserve every UNORM byte for equal-sized identity copies.
    float4 color=options.y ? image.SampleLevel(imageSampler,uv,0) :
        image.Load(int3(clamp(int2(uv*float2(width,height)),int2(0,0),int2(width,height)-1),0));
    if(options.x==2) {
        precise float4 product=color*multiplyColor;
        color=product+addColor;
    }
    return color;
}
