/**********************************************************************
Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#pragma once

#include <algorithm>
#include <cmath>

#if defined(_WIN32) && !defined(NO_MIN_MAX)
#undef MIN
#undef MAX
#undef min
#undef max
#endif

namespace capsaicin
{
class float2
{
public:
    float2(float xx = 0.f, float yy = 0.f) : x(xx), y(yy) {}
    float2(const float* v) : x(v[0]), y(v[1]) {}

    float& operator[](int i) { return *(&x + i); }
    float operator[](int i) const { return *(&x + i); }
    float2 operator-() const { return float2(-x, -y); }

    float sqnorm() const { return x * x + y * y; }
    void normalize() { (*this) /= (std::sqrt(sqnorm())); }

    float2& operator+=(float2 const& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    float2& operator-=(float2 const& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    float2& operator*=(float2 const& o)
    {
        x *= o.x;
        y *= o.y;
        return *this;
    }
    float2& operator*=(float c)
    {
        x *= c;
        y *= c;
        return *this;
    }
    float2& operator/=(float c)
    {
        float cinv = 1.f / c;
        x *= cinv;
        y *= cinv;
        return *this;
    }

    float x, y;
};

class float3
{
public:
    float3(float xx = 0.f, float yy = 0.f, float zz = 0.f) : x(xx), y(yy), z(zz) {}
    float3(const float* v) : x(v[0]), y(v[1]), z(v[2]) {}

    float& operator[](int i) { return *(&x + i); }
    float operator[](int i) const { return *(&x + i); }
    float3 operator-() const { return float3(-x, -y, -z); }

    float sqnorm() const { return x * x + y * y + z * z; }
    void normalize() { (*this) /= (std::sqrt(sqnorm())); }
    float3 inverse() const { return float3(1.f / x, 1.f / y, 1.f / z); }

    float3& operator+=(float3 const& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    float3& operator-=(float3 const& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    float3& operator*=(float3 const& o)
    {
        x *= o.x;
        y *= o.y;
        z *= o.z;
        return *this;
    }
    float3& operator*=(float c)
    {
        x *= c;
        y *= c;
        z *= c;
        return *this;
    }
    float3& operator/=(float c)
    {
        float cinv = 1.f / c;
        x *= cinv;
        y *= cinv;
        z *= cinv;
        return *this;
    }
    float3& operator/=(float3 const& v)
    {
        x /= v.x;
        y /= v.y;
        z /= v.z;
        return *this;
    }

    float x, y, z;
};

class int2
{
public:
    int2(int xx = 0, int yy = 0) : x(xx), y(yy) {}

    int& operator[](int i) { return *(&x + i); }
    int operator[](int i) const { return *(&x + i); }
    int2 operator-() const { return int2(-x, -y); }

    int sqnorm() const { return x * x + y * y; }

    operator float2() { return float2((float)x, (float)y); }

    int2& operator+=(int2 const& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    int2& operator-=(int2 const& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    int2& operator*=(int2 const& o)
    {
        x *= o.x;
        y *= o.y;
        return *this;
    }
    int2& operator*=(int c)
    {
        x *= c;
        y *= c;
        return *this;
    }

    int x, y;
};

class int3
{
public:
    int3(int xx = 0, int yy = 0, int zz = 0) : x(xx), y(yy), z(zz) {}

    int& operator[](int i) { return *(&x + i); }
    int operator[](int i) const { return *(&x + i); }
    int3 operator-() const { return int3(-x, -y, -z); }

    int sqnorm() const { return x * x + y * y + z * z; }

    operator float3() { return float3((float)x, (float)y, (float)z); }

    bool operator==(const int3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const int3& other) const { return !operator==(other); }

    int3& operator+=(int3 const& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    int3& operator-=(int3 const& o)
    {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    int3& operator*=(int3 const& o)
    {
        x *= o.x;
        y *= o.y;
        z *= o.z;
        return *this;
    }
    int3& operator*=(int c)
    {
        x *= c;
        y *= c;
        z *= c;
        return *this;
    }

    int x, y, z;
};

inline int2 operator+(int2 const& v1, int2 const& v2)
{
    int2 res = v1;
    return res += v2;
}

inline int2 operator-(int2 const& v1, int2 const& v2)
{
    int2 res = v1;
    return res -= v2;
}

inline int2 operator*(int2 const& v1, int2 const& v2)
{
    int2 res = v1;
    return res *= v2;
}

inline int2 operator*(int2 const& v1, int c)
{
    int2 res = v1;
    return res *= c;
}

inline int2 operator*(int c, int2 const& v1) { return operator*(v1, c); }

inline int dot(int2 const& v1, int2 const& v2) { return v1.x * v2.x + v1.y * v2.y; }

inline int2 vmin(int2 const& v1, int2 const& v2) { return int2(std::min(v1.x, v2.x), std::min(v1.y, v2.y)); }

inline int2 vmax(int2 const& v1, int2 const& v2) { return int2(std::max(v1.x, v2.x), std::max(v1.y, v2.y)); }

inline int3 operator+(int3 const& v1, int3 const& v2)
{
    int3 res = v1;
    return res += v2;
}

inline int3 operator-(int3 const& v1, int3 const& v2)
{
    int3 res = v1;
    return res -= v2;
}

inline int3 operator*(int3 const& v1, int3 const& v2)
{
    int3 res = v1;
    return res *= v2;
}

inline int3 operator*(int3 const& v1, int c)
{
    int3 res = v1;
    return res *= c;
}

inline int3 operator*(int c, int3 const& v1) { return operator*(v1, c); }

inline int dot(int3 const& v1, int3 const& v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }

inline int3 vmin(int3 const& v1, int3 const& v2)
{
    return int3(std::min(v1.x, v2.x), std::min(v1.y, v2.y), std::min(v1.z, v2.z));
}

inline int3 vmax(int3 const& v1, int3 const& v2)
{
    return int3(std::max(v1.x, v2.x), std::max(v1.y, v2.y), std::max(v1.z, v2.z));
}

inline float2 operator+(float2 const& v1, float2 const& v2)
{
    float2 res = v1;
    return res += v2;
}

inline float2 operator-(float2 const& v1, float2 const& v2)
{
    float2 res = v1;
    return res -= v2;
}

inline float2 operator*(float2 const& v1, float2 const& v2)
{
    float2 res = v1;
    return res *= v2;
}

inline float2 operator*(float2 const& v1, float c)
{
    float2 res = v1;
    return res *= c;
}

inline float2 operator*(float c, float2 const& v1) { return operator*(v1, c); }

inline float dot(float2 const& v1, float2 const& v2) { return v1.x * v2.x + v1.y * v2.y; }

inline float2 normalize(float2 const& v)
{
    float2 res = v;
    res.normalize();
    return res;
}

inline float2 vmin(float2 const& v1, float2 const& v2) { return float2(std::min(v1.x, v2.x), std::min(v1.y, v2.y)); }

inline float2 vmax(float2 const& v1, float2 const& v2) { return float2(std::max(v1.x, v2.x), std::max(v1.y, v2.y)); }

inline float3 operator+(float3 const& v1, float3 const& v2)
{
    float3 res = v1;
    return res += v2;
}

inline float3 operator-(float3 const& v1, float3 const& v2)
{
    float3 res = v1;
    return res -= v2;
}

inline float3 operator*(float3 const& v1, float3 const& v2)
{
    float3 res = v1;
    return res *= v2;
}

inline float3 operator*(float3 const& v1, float c)
{
    float3 res = v1;
    return res *= c;
}

inline float3 operator/(float3 const& v1, float3 const& v2)
{
    float3 res = v1;
    return res /= v2;
}

inline float3 operator*(float c, float3 const& v1) { return operator*(v1, c); }

inline float dot(float3 const& v1, float3 const& v2) { return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; }

inline float3 normalize(float3 const& v)
{
    float3 res = v;
    res.normalize();
    return res;
}

inline float3 cross(float3 const& v1, float3 const& v2)
{
    /// |i     j     k|
    /// |v1x  v1y  v1z|
    /// |v2x  v2y  v2z|
    return float3(v1.y * v2.z - v2.y * v1.z, v2.x * v1.z - v1.x * v2.z, v1.x * v2.y - v1.y * v2.x);
}

inline float3 vmin(float3 const& v1, float3 const& v2)
{
    return float3(std::min(v1.x, v2.x), std::min(v1.y, v2.y), std::min(v1.z, v2.z));
}

inline void vmin(float3 const& v1, float3 const& v2, float3& v)
{
    v.x = std::min(v1.x, v2.x);
    v.y = std::min(v1.y, v2.y);
    v.z = std::min(v1.z, v2.z);
}

inline float3 vmax(float3 const& v1, float3 const& v2)
{
    return float3(std::max(v1.x, v2.x), std::max(v1.y, v2.y), std::max(v1.z, v2.z));
}

inline void vmax(float3 const& v1, float3 const& v2, float3& v)
{
    v.x = std::max(v1.x, v2.x);
    v.y = std::max(v1.y, v2.y);
    v.z = std::max(v1.z, v2.z);
}

class Aabb
{
public:
    Aabb()
        : pmin(float3(std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max())),
          pmax(float3(-std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max()))
    {
    }

    Aabb(float3 const& p) : pmin(p), pmax(p) {}

    Aabb(float3 const& p1, float3 const& p2) : pmin(vmin(p1, p2)), pmax(vmax(p1, p2)) {}

    float3 center() const { return 0.5f * (pmax + pmin); }
    float3 extents() const { return pmax - pmin; }

    bool contains(float3 const& p) const;

    int maxdim() const;

    float surface_area() const
    {
        float3 ext = extents();
        return 2.f * (ext.x * ext.y + ext.x * ext.z + ext.y * ext.z);
    }

    // TODO: this is non-portable, optimization trial for fast intersection test
    float3 const& operator[](int i) const { return *(&pmin + i); }

    // Grow the bounding box by a point
    void grow(float3 const& p)
    {
        vmin(pmin, p, pmin);
        vmax(pmax, p, pmax);
    }
    // Grow the bounding box by a box
    void grow(Aabb const& b)
    {
        vmin(pmin, b.pmin, pmin);
        vmax(pmax, b.pmax, pmax);
    }

    float3 pmin;
    float3 pmax;
};

#define BBOX_INTERSECTION_EPS 1e-5f

inline bool Aabb::contains(float3 const& p) const
{
    return p.x <= pmax.x && p.x >= pmin.x && p.y <= pmax.y && p.y >= pmin.y && p.z <= pmax.z && p.z >= pmin.z;
}

inline Aabb bboxunion(Aabb const& box1, Aabb const& box2)
{
    Aabb res;
    res.pmin = vmin(box1.pmin, box2.pmin);
    res.pmax = vmax(box1.pmax, box2.pmax);
    return res;
}

inline Aabb intersection(Aabb const& box1, Aabb const& box2)
{
    return Aabb(vmax(box1.pmin, box2.pmin), vmin(box1.pmax, box2.pmax));
}

inline void intersection(Aabb const& box1, Aabb const& box2, Aabb& box)
{
    vmax(box1.pmin, box2.pmin, box.pmin);
    vmin(box1.pmax, box2.pmax, box.pmax);
}

inline bool intersects(Aabb const& box1, Aabb const& box2)
{
    float3 b1c = box1.center();
    float3 b1r = 0.5f * box1.extents();
    float3 b2c = box2.center();
    float3 b2r = 0.5f * box2.extents();

    return (fabs(b2c.x - b1c.x) - (b1r.x + b2r.x)) <= BBOX_INTERSECTION_EPS &&
           (fabs(b2c.y - b1c.y) - (b1r.y + b2r.y)) <= BBOX_INTERSECTION_EPS &&
           (fabs(b2c.z - b1c.z) - (b1r.z + b2r.z)) <= BBOX_INTERSECTION_EPS;
}

inline bool contains(Aabb const& box1, Aabb const& box2)
{
    return box1.contains(box2.pmin) && box1.contains(box2.pmax);
}

inline int Aabb::maxdim() const
{
    float3 ext = extents();

    if (ext.x >= ext.y && ext.x >= ext.z)
        return 0;
    if (ext.y >= ext.x && ext.y >= ext.z)
        return 1;
    if (ext.z >= ext.x && ext.z >= ext.y)
        return 2;

    return 0;
}

inline float2 IntersectAabb(const Aabb& aabb, float3 ray_origin, float3 ray_inv_dir, float t_max)
{
    const float3 box_min_rel = aabb.pmin - ray_origin;
    const float3 box_max_rel = aabb.pmax - ray_origin;

    const float3 t_plane_min = box_min_rel * ray_inv_dir;
    const float3 t_plane_max = box_max_rel * ray_inv_dir;

    float3 min_interval, max_interval;

    min_interval.x = ray_inv_dir.x >= 0.0f ? t_plane_min.x : t_plane_max.x;
    max_interval.x = ray_inv_dir.x >= 0.0f ? t_plane_max.x : t_plane_min.x;

    min_interval.y = ray_inv_dir.y >= 0.0f ? t_plane_min.y : t_plane_max.y;
    max_interval.y = ray_inv_dir.y >= 0.0f ? t_plane_max.y : t_plane_min.y;

    min_interval.z = ray_inv_dir.z >= 0.0f ? t_plane_min.z : t_plane_max.z;
    max_interval.z = ray_inv_dir.z >= 0.0f ? t_plane_max.z : t_plane_min.z;

    const float min_of_intervals = std::max(std::max(min_interval.x, min_interval.y), min_interval.z);
    const float max_of_intervals = std::min(std::min(max_interval.x, max_interval.y), max_interval.z);

    const float min_t = std::max(min_of_intervals, 0.0f);
    const float max_t = std::min(max_of_intervals, t_max);

    return float2(min_t, max_t);
}
}  // namespace capsaicin