#define TILE_SIZE 8

struct Constants
{
    uint width;
    uint height;
    uint frame_count;
    uint type;
};

ConstantBuffer<Constants> g_constants : register(b0);
RWTexture2D<float4> g_output_color_direct : register(u0);
RWTexture2D<float4> g_output_color_indirect : register(u1);
RWTexture2D<float4> g_gbuffer_albedo: register(u2);

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void Combine(in uint2 gidx: SV_DispatchThreadID,
             in uint2 lidx: SV_GroupThreadID,
             in uint2 bidx: SV_GroupID)
{
    if (gidx.x >= g_constants.width || gidx.y >= g_constants.height)
        return;

    float4 indirect = float4(g_output_color_indirect[gidx].xyz, 1.f);

    switch (g_constants.type)
    {
        case 0:
            g_output_color_indirect[gidx] = indirect * g_gbuffer_albedo[gidx] + g_output_color_direct[gidx];
            return;
        case 1:
            g_output_color_indirect[gidx] = g_output_color_direct[gidx];
            return;
        case 2:
            g_output_color_indirect[gidx] = indirect;
            return;
        case 3:
            g_output_color_indirect[gidx] = float4(g_output_color_indirect[gidx].www, 1.f);
            return; 
    }
}
