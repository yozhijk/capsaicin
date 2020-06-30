#ifndef COLOR_SPACE_H
#define COLOR_SPACE_H

float Luminance(in float3 rgb)
{
    return dot(rgb, float3(0.299f, 0.587f, 0.114f));
}

float3 RGB2YCoCg(float3 c)
{
    // Y = R/4 + G/2 + B/4
    // Co = R/2 - B/2
    // Cg = -R/4 + G/2 - B/4
    return float3(c.x / 4.0 + c.y / 2.0 + c.z / 4.0,
                  c.x / 2.0 - c.z / 2.0,
                  -c.x / 4.0 + c.y / 2.0 - c.z / 4.0);
}

float3 YCoCg2RGB(float3 c)
{
    // R = Y + Co - Cg
    // G = Y + Cg
    // B = Y - Co - Cg
    return clamp(float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z), 0.f, 1.f);
}

float3 SimpleTonemap(float3 v)
{
    return v / (1.f + Luminance(v));
}

float3 InvertSimpleTonemap(float3 v)
{
    return v / (1.f - Luminance(v));
}

#endif  // COLOR_SPACE_H