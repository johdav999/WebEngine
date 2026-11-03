#pragma once
#include "engine/cloth.hpp"
#include <optional>

namespace engine::cloth_detail {
inline float magnitude(Vec3 v){return std::sqrt(dot(v,v));}
struct Closest {Vec3 point;std::array<float,3> weights{};Vec3 fallback{0,1,0};};
inline Closest edges(Vec3 p,ClothTriangle t) {
        Closest best{t.a,{1,0,0}};float distance=dot(p-t.a,p-t.a);
        const std::array<Vec3,3> v{t.a,t.b,t.c};
        for(unsigned i=0;i<3;++i){auto j=(i+1)%3;auto d=v[j]-v[i];float l=dot(d,d);
            float u=l>1e-12f?std::clamp(dot(p-v[i],d)/l,0.f,1.f):0;
            auto q=v[i]+d*u;float candidate=dot(p-q,p-q);
            if(candidate<distance){distance=candidate;best.point=q;best.weights={};best.weights[i]=1-u;best.weights[j]=u;}}
        return best;
}
inline Closest triangle(Vec3 p,ClothTriangle t) {
    auto ab=t.b-t.a,ac=t.c-t.a,n=cross(ab,ac);
    // A simulated face may collapse; use its closest edge rather than divide by area.
    if(dot(n,n)<1e-12f)return edges(p,t);
    n=normalized(n);
    auto make=[&](float a,float b,float c){return Closest{t.a*a+t.b*b+t.c*c,{a,b,c},n};};
    auto ap=p-t.a;float d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return make(1,0,0);
    auto bp=p-t.b;float d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return make(0,1,0);
    float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){if(d1-d3<=1e-12f)return edges(p,t);float v=d1/(d1-d3);return make(1-v,v,0);}
    auto cp=p-t.c;float d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return make(0,0,1);
    float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){if(d2-d6<=1e-12f)return edges(p,t);float w=d2/(d2-d6);return make(1-w,0,w);}
    float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){float sum=(d4-d3)+(d5-d6);if(sum<=1e-12f)return edges(p,t);float w=(d4-d3)/sum;return make(0,1-w,w);}
    float sum=va+vb+vc;if(sum<=1e-20f||!std::isfinite(sum))return edges(p,t);
    float inverse=1/sum;float v=vb*inverse,w=vc*inverse;return make(1-v-w,v,w);
}
inline bool triangleValid(ClothTriangle t) {auto n=cross(t.b-t.a,t.c-t.a);return dot(n,n)>=1e-12f;}
inline ClothTriangle interpolate(ClothTriangle a,ClothTriangle b,float t) {
    return {a.a+(b.a-a.a)*t,a.b+(b.b-a.b)*t,a.c+(b.c-a.c)*t};
}
inline bool overlaps(Vec3 from,Vec3 to,ClothTriangle a,ClothTriangle b,float radius) {
    for(unsigned axis=0;axis<3;++axis) {
        auto value=[&](Vec3 p){return axis==0?p.x:axis==1?p.y:p.z;};
        float lo=std::min({value(a.a),value(a.b),value(a.c),value(b.a),value(b.b),value(b.c)})-radius;
        float hi=std::max({value(a.a),value(a.b),value(a.c),value(b.a),value(b.b),value(b.c)})+radius;
        if(std::max(value(from),value(to))<lo||std::min(value(from),value(to))>hi)return false;
    }
    return true;
}
struct Hit {Closest closest;Vec3 normal;float time=1,separation=0;};
// Distance to a linearly moving convex feature is Lipschitz bounded by the
// point speed plus maximum feature-vertex speed. A bounded conservative advance
// stops early on exhaustion (possible false contacts, never an unchecked jump).
template<class Sample> std::optional<Hit> sweep(Vec3 from,Vec3 to,float radius,float speed,Sample sample,bool swept) {
    float time=swept?0.f:1.f;
    for(unsigned iteration=0;iteration<64;++iteration) {
        auto p=from+(to-from)*time;auto closest=sample(time,p);auto d=p-closest.point;float distance=magnitude(d);
        if(distance<=radius+1e-5f||iteration==63) {
            auto normal=distance>1e-7f?d*(1/distance):closest.fallback;
            return Hit{closest,normal,time,std::max(radius,distance)};
        }
        if(!swept||speed<1e-8f)return {};
        float next=time+.9f*(distance-radius)/speed;
        if(next>1)return {};
        if(next<=time)return Hit{closest,normalized(d),time,std::max(radius,distance)};
        time=next;
    }
    return {};
}
inline std::optional<Hit> surface(Vec3 from,Vec3 to,ClothTriangle a,ClothTriangle b,float radius,bool swept) {
    if(!overlaps(swept?from:to,to,swept?a:b,b,radius+1e-5f))return {};
    float speed=magnitude(to-from)+std::max({magnitude(b.a-a.a),magnitude(b.b-a.b),magnitude(b.c-a.c)});
    return sweep(from,to,radius,speed,[&](float t,Vec3 p){return triangle(p,interpolate(a,b,t));},swept);
}
inline Closest capsule(Vec3 p,ClothCapsule c) {
    auto axis=c.b-c.a;float size=dot(axis,axis);
    auto q=c.a+axis*(size>1e-12f?std::clamp(dot(p-c.a,axis)/size,0.f,1.f):0.f);
    auto normal=normalized(cross(axis,{1,0,0}));if(dot(normal,normal)<.5f)normal={0,0,1};
    return {q,{},normal};
}
inline std::optional<Hit> box(Vec3 from,Vec3 to,Vec3 lo,Vec3 hi) {
    if(from.x>lo.x&&from.x<hi.x&&from.y>lo.y&&from.y<hi.y&&from.z>lo.z&&from.z<hi.z)return {};
    float enter=0,leave=1;Vec3 normal{};auto motion=to-from;
    for(unsigned axis=0;axis<3;++axis) {
        auto value=[&](Vec3 p){return axis==0?p.x:axis==1?p.y:p.z;};
        float d=value(motion),origin=value(from);
        if(std::abs(d)<1e-9f){if(origin<value(lo)||origin>value(hi))return {};continue;}
        float a=(value(lo)-origin)/d,b=(value(hi)-origin)/d;
        Vec3 n=axis==0?Vec3{-1,0,0}:axis==1?Vec3{0,-1,0}:Vec3{0,0,-1};
        if(a>b){std::swap(a,b);n=n*-1;}
        if(a>=enter){enter=a;normal=n;}leave=std::min(leave,b);
        if(enter>leave)return {};
    }
    if(dot(normal,normal)<.5f||dot(motion,normal)>=0)return {};
    return Hit{Closest{from+motion*enter},normal,enter};
}
struct Edge {Vec3 a,b;};
struct EdgeClosest {Vec3 a,b,normal;float s=0,t=0;};
inline EdgeClosest segments(Edge first,Edge second) {
    const auto u=first.b-first.a,v=second.b-second.a,r=first.a-second.a;
    auto wideDot=[](Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;};
    double a=wideDot(u,u),b=wideDot(u,v),c=wideDot(v,v),d=wideDot(u,r),e=wideDot(v,r),s=0,t=0;
    if(a<=1e-12){if(c>1e-12)t=std::clamp(e/c,0.0,1.0);}
    else if(c<=1e-12)s=std::clamp(-d/a,0.0,1.0);
    else {
        double denominator=a*c-b*b;
        if(denominator>1e-12*a*c)s=std::clamp((b*e-c*d)/denominator,0.0,1.0);
        else {
            // Parallel overlap: choose its middle to avoid arbitrary endpoint bias.
            double start=-d/a,end=start+b/a;
            double lo=std::max(0.0,std::min(start,end)),hi=std::min(1.0,std::max(start,end));
            if(lo<=hi)s=(lo+hi)*.5;
        }
        t=(b*s+e)/c;
        if(t<0){t=0;s=std::clamp(-d/a,0.0,1.0);}
        else if(t>1){t=1;s=std::clamp((b-d)/a,0.0,1.0);}
    }
    auto p=first.a+u*float(s),q=second.a+v*float(t),delta=p-q;float distance=magnitude(delta);
    Vec3 normal=distance>1e-7f?delta*(1/distance):normalized(cross(u,v));
    if(dot(normal,normal)<.5f) {
        auto direction=a>=c?u:v;
        Vec3 axis=std::abs(direction.x)<=std::abs(direction.y)&&std::abs(direction.x)<=std::abs(direction.z)?Vec3{1,0,0}:
                  std::abs(direction.y)<=std::abs(direction.z)?Vec3{0,1,0}:Vec3{0,0,1};
        normal=normalized(cross(direction,axis));if(dot(normal,normal)<.5f)normal={0,1,0};
    }
    return {p,q,normal,float(s),float(t)};
}
struct EdgeHit {EdgeClosest closest;float time=1,separation=0;};
inline std::optional<EdgeHit> edgeSweep(Edge a0,Edge a1,Edge b0,Edge b1,float radius,bool swept) {
    float speed=std::max(magnitude(a1.a-a0.a),magnitude(a1.b-a0.b))+
                std::max(magnitude(b1.a-b0.a),magnitude(b1.b-b0.b));
    auto mix=[](Edge a,Edge b,float t){return Edge{a.a+(b.a-a.a)*t,a.b+(b.b-a.b)*t};};
    float time=swept?0.f:1.f;
    for(unsigned iteration=0;iteration<64;++iteration) {
        auto closest=segments(mix(a0,a1,time),mix(b0,b1,time));float distance=magnitude(closest.a-closest.b);
        if(distance<=radius+1e-5f||iteration==63)return EdgeHit{closest,time,std::max(radius,distance)};
        if(!swept||speed<1e-8f)return {};
        float next=time+.9f*(distance-radius)/speed;
        if(next>1)return {};
        if(next<=time)return EdgeHit{closest,time,std::max(radius,distance)};
        time=next;
    }
    return {};
}
} // namespace engine::cloth_detail
