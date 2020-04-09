struct VsOutput
{
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

struct Constants
{
    uint width;
    uint height;
    float time;
    uint padding;
};

ConstantBuffer<Constants> g_constants : register(b0);
Texture2D<float4> g_raytraced_texture_direct : register(t0);
Texture2D<float4> g_raytraced_texture_indirect : register(t1);

VsOutput VsMain(uint vid: SV_VertexID)
{
    VsOutput output;
    // Full screen quad
    if (vid == 0)
    {
        output.pos = float4(-1.f, -3.0f, 0.1f, 1.f);
        output.uv = float2(0.f, 2.f);
    }
    else if (vid == 1)
    {
        output.pos =  float4(-1.0f, 1.f, 0.1f, 1.f);
        output.uv = float2(0.f, 0.f);
    }
    else
    {
        output.pos =  float4(3.0f, 1.f, 0.1f, 1.f);
        output.uv = float2(2.f, 0.f);
    }
    return output;
}

float4 PsMain(VsOutput input) : SV_TARGET
{
    int3 pixel;
    pixel.x = int(input.uv.x * g_constants.width);
    pixel.y = int((1.f - input.uv.y) * g_constants.height);
    pixel.z = 0;
    return g_raytraced_texture_direct.Load(pixel) + g_raytraced_texture_indirect.Load(pixel);
}