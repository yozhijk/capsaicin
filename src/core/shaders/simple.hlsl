struct VsOutput
{
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

struct Constants
{
    float time;
};

ConstantBuffer<Constants> g_constants : register(b0);

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

bool IntersectTriangle(float3 v0, float3 v1, float3 v2, float3 ro, float3 rd, float mint, float maxt, out float2 uv, out float t)
{
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    float3 s1 = cross(rd, e2);

    float denom = dot(s1, e1);

    if (abs(denom) < 1e-5)
    {
       return false;
    }

    float invd = 1.0f / denom;
    float3 d = ro - v0;
    float b1 = dot(d, s1) * invd;

    float3 s2 = cross(d, e1);
    float b2 = dot(rd, s2) * invd;

    float temp = dot(e2, s2) * invd;

    if ((b1 < 0.f) || (b1 > 1.f) ||
        (b2 < 0.f) || (b1 + b2 > 1.f) ||
        (temp < mint) || (temp > maxt))
    {
        return false;
    }
    else
    {
        uv.x = b1;
        uv.y = b2;
        t = temp;
        return true;
    }
}

float4 mod(float4 x, float4 y)
{
  return x - y * floor(x / y);
}

float4 mod289(float4 x)
{
  return x - floor(x / 289.0) * 289.0;
}

float4 permute(float4 x)
{
  return mod289(((x*34.0)+1.0)*x);
}

float4 taylorInvSqrt(float4 r)
{
  return (float4)1.79284291400159 - r * 0.85373472095314;
}

float2 fade(float2 t) {
  return t*t*t*(t*(t*6.0-15.0)+10.0);
}

// Classic Perlin noise
float cnoise(float2 P)
{
  float4 Pi = floor(P.xyxy) + float4(0.0, 0.0, 1.0, 1.0);
  float4 Pf = frac (P.xyxy) - float4(0.0, 0.0, 1.0, 1.0);
  Pi = mod289(Pi); // To avoid truncation effects in permutation
  float4 ix = Pi.xzxz;
  float4 iy = Pi.yyww;
  float4 fx = Pf.xzxz;
  float4 fy = Pf.yyww;

  float4 i = permute(permute(ix) + iy);

  float4 gx = frac(i / 41.0) * 2.0 - 1.0 ;
  float4 gy = abs(gx) - 0.5 ;
  float4 tx = floor(gx + 0.5);
  gx = gx - tx;

  float2 g00 = float2(gx.x,gy.x);
  float2 g10 = float2(gx.y,gy.y);
  float2 g01 = float2(gx.z,gy.z);
  float2 g11 = float2(gx.w,gy.w);

  float4 norm = taylorInvSqrt(float4(dot(g00, g00), dot(g01, g01), dot(g10, g10), dot(g11, g11)));
  g00 *= norm.x;
  g01 *= norm.y;
  g10 *= norm.z;
  g11 *= norm.w;

  float n00 = dot(g00, float2(fx.x, fy.x));
  float n10 = dot(g10, float2(fx.y, fy.y));
  float n01 = dot(g01, float2(fx.z, fy.z));
  float n11 = dot(g11, float2(fx.w, fy.w));

  float2 fade_xy = fade(Pf.xy);
  float2 n_x = lerp(float2(n00, n01), float2(n10, n11), fade_xy.x);
  float n_xy = lerp(n_x.x, n_x.y, fade_xy.y);
  return 2.3 * n_xy;
}

float GetSurfaceHeight(float2 uv)
{
    return cnoise(uv * 15.f);


    float h = sin(uv.y * 30.f) * cos(uv.x * 13.f) * 0.5f + 0.5f;
    if (h > 0.75) return 1.f;
    else if (h > 0.5) return 0.75f;
    else if (h > 0.25) return 0.5f;
    else if (h > 0.1) return 0.25f;
    else return 0.f;
}

float2 FindParallaxUV(float3 d_tan, float2 uv)
{
    float2 duv = 0.1f * d_tan.xz / (-d_tan.y);

    float2 curr_uv = uv;
    float2 prev_uv = uv;

    int num_samples  = (int)lerp(8.f, 128.f, 1.f - d_tan.y);
    const float dz = 1.f / num_samples;
    duv *= dz;

    float height = 0.f;
    float prev_height = 0.f;

    float  z = 0.f;

    for(;z <= 1.f ; z += dz)
    {
        height = GetSurfaceHeight(curr_uv);

        if (1.f - z <= height)
        {
            float t = saturate(1.f - (prev_height - (1.f - 2.f * z)) / ((1.f - z) - (1.f - 2.f * z) - (prev_height - height)));
            curr_uv = lerp(prev_uv, curr_uv, t);
            break;
        }

        prev_height = height;
        prev_uv = curr_uv;

        curr_uv += duv;
    }

    return curr_uv;
}

float TraceHeightmapShadowRay(float3 d_tan, float2 uv)
{
    float2 duv = -0.1f * d_tan.xz / d_tan.y;
    int num_samples  = (int)lerp(8.f, 128.f, 1.f - d_tan.y);

    float height = GetSurfaceHeight(uv);

    const float dz = (1.f - height) / num_samples;
    duv *= dz;

    if (dz < 1e-5f) return 1.f;

    float2 curr_uv = uv;

    for(float z = 1.f - height; z >= 0.f; z-=dz)
    {
        height = GetSurfaceHeight(curr_uv);

        if (1.f - z < height)
        {
            return 0.f;
        }

        curr_uv += duv;
    }

    return 1.f;
}





bool IntersectQuad(float3 v0, float3 v1, float3 v2, 
                   float3 v3, float3 ro, float3 rd,
                   float mint, float maxt, out float2 uv, out float t)
{
    float2 b;
    if (IntersectTriangle(v0, v1, v2, ro, rd, mint, maxt, b, t))
    {
        // remap.
        uv.x = b.x + b.y;
        uv.y = b.y;
        return true;
    } else if (IntersectTriangle(v0, v2, v3, ro, rd, mint, maxt, b, t))
    {
        uv.x = b.x;
        uv.y = b.x + b.y;
        return true;
    } else return false;
}

float3 EstimateNormal(float3x3 tan2w, float2 uv)
{
    float hp = GetSurfaceHeight(uv);
    float hpl = GetSurfaceHeight(uv - float2(1.f / 512.f, 0.f));
    float hpr = GetSurfaceHeight(uv + float2(1.f / 512.f, 0.f));
    float hpt = GetSurfaceHeight(uv - float2(0.f, 1.f / 512.f));
    float hpb = GetSurfaceHeight(uv + float2(0.f, 1.f / 512.f));

    float3 en = normalize(0.25f * float3(2.f * (hpr - hpl), 1.f, 2.f * (hpb - hpt)));

    return mul(tan2w, en);
}


float3 EstimateCavity(float2 uv, float angle)
{
    float2 offsets[] = 
    {
        float2(-1,2),
        float2(2,2),
        float2(-3,-2),
        float2(-3,4),
        float2(3,-3),
        float2(-6,2),
        float2(0,-4),
        float2(6,1),
        float2(-7,-1),
        float2(-1,7),
        float2(1,-7),
        float2(4,5),
        float2(-4,-6),
        float2(6,-4),
        float2(7,4)
    };

    float h = GetSurfaceHeight(uv);

    float occluded = 0.f;

    for (int i = 0; i < 15; ++i)
    {
        float2 s = offsets[i];
        //float2 rotated = float2(cos(angle) * s.x + sin(angle) * s.y, -sin(angle) * s.x + cos(angle) * s.y); 
        float hp = GetSurfaceHeight(uv + float2(1.f / 4096.f, 1.f / 4096.f) * s);

        if (hp > h + 0.1f)
            occluded += 1.f;
    }

    return 1.f - occluded / 15.f;
}



float4 PsMain(VsOutput input) : SV_TARGET
{
    float sw = 0.08f;
    float sh = 0.06f;
    float z_near = 0.01f;
    float4 total_color = 0.f;

    float3 v0 = float3(-50.f, 0.f, -50.f);
    float3 v1 = float3(50.f, 0.f, -50.f);
    float3 v2 = float3(50.f, 0.f, 50.f);
    float3 v3 = float3(-50.f, 0.f, 50.f);

    float3 du = normalize(v1 - v0);
    float3 dv = normalize(v3 - v0);
    float3 n = cross(dv, du);


    float3x3 w2tan = float3x3(du, n, dv);
    float3x3 tan2w = transpose(w2tan);

    for (int xoff = 0; xoff <= 1; ++xoff)
    for (int yoff = 0; yoff <= 1; ++yoff)
    {
    float3 o = float3(0.5f * sin(g_constants.time * 0.2f), 1.f + 0.2f * cos(g_constants.time * 0.1f), -7.f + 50 * sin(g_constants.time * 0.002f));
    float3 d = normalize(float3(lerp(-sw/2.f, sw/2.f, input.uv.x + ddx(input.uv.x) * 0.5f * xoff), lerp(-sh/2.f, sh/2.f, 1.f - input.uv.y - ddy(input.uv.y) * 0.5f * yoff), z_near));

    float3 d_tan = mul(w2tan, d);


    float t;
    float2 uv;
    if (IntersectQuad(v0, v1, v2, v3, o, d, 0.1f, 100.f, uv, t))
    {
        uv *= 100.f;
        float2 parallax_uv = FindParallaxUV(d_tan, uv);
        float3 en = EstimateNormal(tan2w, parallax_uv);
        const float3 light = normalize(float3(-1.f, -1.f, 1.f));
        const float3 l_tan = mul(w2tan, -light);
        float n_dot_l = saturate(dot(-light, en));
        float h = GetSurfaceHeight(parallax_uv);
        float shadow = 1.f;//TraceHeightmapShadowRay(l_tan, parallax_uv);
        float cavity = EstimateCavity(parallax_uv, input.uv.x * 3.14);

        float3 color = lerp(float3(0.2f, 0.6f, 0.2f), float3(1.f, 1.f, 1.f), h);
        total_color += float4(cavity * color * shadow * n_dot_l, 1.f);
    }
    }
    return total_color * 0.25f;
}