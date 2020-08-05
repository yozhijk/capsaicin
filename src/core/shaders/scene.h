// clang-format off
#ifndef SCENE_H
#define SCENE_H

void InterpolateAttributes(in uint instance_index,
                           in uint prim_index,
                           in float2 uv,
                           out float3 p,
                           out float3 n,
                           out float2 tx)
{
    Mesh mesh = g_mesh_buffer[instance_index];

    uint io = mesh.first_index_offset;
    uint i0 = g_index_buffer[io + 3 * prim_index + 0];
    uint i1 = g_index_buffer[io + 3 * prim_index + 1];
    uint i2 = g_index_buffer[io + 3 * prim_index + 2];

    uint vo = mesh.first_vertex_offset * 3;

    float3 v0 = float3(g_vertex_buffer[vo + 3 * i0],
                       g_vertex_buffer[vo + 3 * i0 + 1],
                       g_vertex_buffer[vo + 3 * i0 + 2]);
    float3 v1 = float3(g_vertex_buffer[vo + 3 * i1],
                       g_vertex_buffer[vo + 3 * i1 + 1],
                       g_vertex_buffer[vo + 3 * i1 + 2]);
    float3 v2 = float3(g_vertex_buffer[vo + 3 * i2],
                       g_vertex_buffer[vo + 3 * i2 + 1],
                       g_vertex_buffer[vo + 3 * i2 + 2]);

    float3 n0 = float3(g_normal_buffer[vo + 3 * i0],
                       g_normal_buffer[vo + 3 * i0 + 1],
                       g_normal_buffer[vo + 3 * i0 + 2]);
    float3 n1 = float3(g_normal_buffer[vo + 3 * i1],
                       g_normal_buffer[vo + 3 * i1 + 1],
                       g_normal_buffer[vo + 3 * i1 + 2]);
    float3 n2 = float3(g_normal_buffer[vo + 3 * i2],
                       g_normal_buffer[vo + 3 * i2 + 1],
                       g_normal_buffer[vo + 3 * i2 + 2]);

    vo = mesh.first_vertex_offset;

    float2 t0 = g_texcoord_buffer[vo + i0];
    float2 t1 = g_texcoord_buffer[vo + i1];
    float2 t2 = g_texcoord_buffer[vo + i2];

    n   = normalize(n0 * (1.f - uv.x - uv.y) + n1 * uv.x + n2 * uv.y);
    p   = v0 * (1.f - uv.x - uv.y) + v1 * uv.x + v2 * uv.y;
    tx  = t0 * (1.f - uv.x - uv.y) + t1 * uv.x + t2 * uv.y;
}

float3 GetMaterial(in uint instance_index, in float2 tx)
{
    Mesh mesh = g_mesh_buffer[instance_index];
    tx.y = 1.f - tx.y;
    float3 kd = (mesh.texture_index == INVALID_ID)
                ? 0.75f
                : g_textures[NonUniformResourceIndex(mesh.texture_index)].SampleLevel(g_sampler, tx, 0);
    kd = pow(kd, 2.2f);
    return kd;
}

#endif // SCENE_H
// clang-format on