#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace engine {
constexpr float pi = 3.14159265358979323846f;
struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline Vec3 normalized(Vec3 v) { const float n = std::sqrt(dot(v,v)); return n > 1e-7f ? v*(1/n) : Vec3{}; }

// Column-major matrices, column vectors, right-handed coordinates; WebGPU depth is [0, 1].
struct Mat4 {
    std::array<float, 16> m{};
    static Mat4 identity() { Mat4 a; a.m[0]=a.m[5]=a.m[10]=a.m[15]=1; return a; }
};
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int c=0; c<4; ++c) for (int r=0; r<4; ++r)
        for (int k=0; k<4; ++k) out.m[c*4+r] += a.m[k*4+r]*b.m[c*4+k];
    return out;
}
inline Vec3 transformPoint(const Mat4& a, Vec3 v) {
    return {a.m[0]*v.x+a.m[4]*v.y+a.m[8]*v.z+a.m[12],
            a.m[1]*v.x+a.m[5]*v.y+a.m[9]*v.z+a.m[13],
            a.m[2]*v.x+a.m[6]*v.y+a.m[10]*v.z+a.m[14]};
}
inline Vec3 transformDirection(const Mat4& a, Vec3 v) {
    return transformPoint(a, v) - transformPoint(a, {});
}
inline Mat4 transpose(const Mat4& a) {
    Mat4 out;
    for(int c=0;c<4;++c) for(int r=0;r<4;++r) out.m[c*4+r]=a.m[r*4+c];
    return out;
}
inline Mat4 inverseAffine(const Mat4& a) {
    const Vec3 x{a.m[0],a.m[1],a.m[2]}, y{a.m[4],a.m[5],a.m[6]}, z{a.m[8],a.m[9],a.m[10]};
    const float det = dot(x,cross(y,z));
    if (std::abs(det)<1e-8f) throw std::invalid_argument("Transform has zero scale");
    const Vec3 r0=cross(y,z)*(1/det), r1=cross(z,x)*(1/det), r2=cross(x,y)*(1/det);
    Mat4 out=Mat4::identity();
    out.m[0]=r0.x; out.m[4]=r0.y; out.m[8]=r0.z;
    out.m[1]=r1.x; out.m[5]=r1.y; out.m[9]=r1.z;
    out.m[2]=r2.x; out.m[6]=r2.y; out.m[10]=r2.z;
    const Vec3 t=transformDirection(out, {a.m[12],a.m[13],a.m[14]})*-1;
    out.m[12]=t.x; out.m[13]=t.y; out.m[14]=t.z;
    return out;
}
struct Transform {
    Vec3 position{}, rotation{}, scale{1,1,1}; // Euler radians: Rz * Ry * Rx.
    Mat4 matrix() const {
        const float cx=std::cos(rotation.x), sx=std::sin(rotation.x);
        const float cy=std::cos(rotation.y), sy=std::sin(rotation.y);
        const float cz=std::cos(rotation.z), sz=std::sin(rotation.z);
        Mat4 rx=Mat4::identity(), ry=rx, rz=rx, s=rx, t=rx;
        rx.m[5]=cx; rx.m[6]=sx; rx.m[9]=-sx; rx.m[10]=cx;
        ry.m[0]=cy; ry.m[2]=-sy; ry.m[8]=sy; ry.m[10]=cy;
        rz.m[0]=cz; rz.m[1]=sz; rz.m[4]=-sz; rz.m[5]=cz;
        s.m[0]=scale.x; s.m[5]=scale.y; s.m[10]=scale.z;
        t.m[12]=position.x; t.m[13]=position.y; t.m[14]=position.z;
        return t*rz*ry*rx*s;
    }
};
inline Mat4 perspective(float fov, float aspect, float nearPlane, float farPlane) {
    if (!(std::isfinite(fov) && std::isfinite(aspect) && std::isfinite(nearPlane) && std::isfinite(farPlane)
        && fov>0 && fov<pi && aspect>0 && nearPlane>0 && farPlane>nearPlane))
        throw std::invalid_argument("Invalid camera projection");
    Mat4 out; const float f=1/std::tan(fov/2);
    out.m[0]=f/aspect; out.m[5]=f; out.m[10]=farPlane/(nearPlane-farPlane);
    out.m[11]=-1; out.m[14]=nearPlane*farPlane/(nearPlane-farPlane);
    for(float value:out.m) if(!std::isfinite(value)) throw std::invalid_argument("Unrepresentable camera projection");
    if(out.m[0]==0 || out.m[5]==0 || out.m[14]==0) throw std::invalid_argument("Degenerate camera projection");
    return out;
}
inline Mat4 orthographic(float height, float aspect, float nearPlane, float farPlane) {
    if (!(std::isfinite(height) && std::isfinite(aspect) && std::isfinite(nearPlane) && std::isfinite(farPlane)
        && height>0 && aspect>0 && nearPlane>=0 && farPlane>nearPlane))
        throw std::invalid_argument("Invalid orthographic projection");
    Mat4 out=Mat4::identity();
    out.m[0]=2/height/aspect; out.m[5]=2/height;
    out.m[10]=1/(nearPlane-farPlane); out.m[14]=nearPlane/(nearPlane-farPlane);
    for(float value:out.m) if(!std::isfinite(value)) throw std::invalid_argument("Unrepresentable orthographic projection");
    if(out.m[0]==0 || out.m[5]==0 || out.m[10]==0) throw std::invalid_argument("Degenerate orthographic projection");
    return out;
}
// General inverse, including projective matrices. Double intermediates reduce picking error.
inline Mat4 inverse(const Mat4& matrix) {
    double a[4][8]{};
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
        if(!std::isfinite(matrix.m[c*4+r])) throw std::invalid_argument("Nonfinite matrix");
        a[r][c]=matrix.m[c*4+r];a[r][c+4]=r==c ? 1 : 0;
    }
    for(int c=0;c<4;++c) {
        int pivot=c;
        for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[pivot][c])) pivot=r;
        if(a[pivot][c]==0) throw std::invalid_argument("Singular matrix");
        for(int k=0;k<8;++k) std::swap(a[c][k],a[pivot][k]);
        const double divisor=a[c][c];
        for(double& value:a[c]) value/=divisor;
        for(int r=0;r<4;++r) if(r!=c) {
            const double factor=a[r][c];
            for(int k=0;k<8;++k) a[r][k]-=factor*a[c][k];
        }
    }
    Mat4 out;
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) {
        out.m[c*4+r]=static_cast<float>(a[r][c+4]);
        if(!std::isfinite(out.m[c*4+r])) throw std::invalid_argument("Unrepresentable matrix inverse");
    }
    return out;
}
} // namespace engine
