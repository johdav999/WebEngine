#include "engine/camera_component.hpp"
#include <limits>

namespace engine {
namespace {
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
void validatePixelViewport(PixelViewport v) {
    if(!(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width) && std::isfinite(v.height)
        && v.width>0 && v.height>0 && std::isfinite(v.width/v.height)))
        throw std::invalid_argument("Invalid pixel viewport");
}
Vec3 unit(Vec3 v) {
    const float length=std::sqrt(dot(v,v));
    if(!finite(v) || !std::isfinite(length) || length<1e-7f) throw std::invalid_argument("Invalid camera direction");
    return v*(1/length);
}
std::array<double,4> homogeneous(const Mat4& m,Vec3 p) {
    std::array<double,4> out{};
    for(int r=0;r<4;++r) out[r]=double(m.m[r])*p.x+double(m.m[4+r])*p.y+double(m.m[8+r])*p.z+m.m[12+r];
    return out;
}
}
bool Frustum::containsPoint(Vec3 point) const {
    if(!finite(point)) throw std::invalid_argument("Invalid frustum point");
    for(const auto& plane:planes) if(plane.signedDistance(point)<0) return false;
    return true;
}
bool Frustum::intersectsSphere(Vec3 center,float radius) const {
    if(!finite(center) || !std::isfinite(radius) || radius<0) throw std::invalid_argument("Invalid sphere");
    for(const auto& plane:planes) if(plane.signedDistance(center)<-radius) return false;
    return true;
}
bool Frustum::intersectsAabb(Vec3 minimum,Vec3 maximum) const {
    if(!finite(minimum) || !finite(maximum) || minimum.x>maximum.x || minimum.y>maximum.y || minimum.z>maximum.z)
        throw std::invalid_argument("Invalid bounds");
    for(const auto& plane:planes) {
        const Vec3 support{plane.normal.x>=0 ? maximum.x : minimum.x,
            plane.normal.y>=0 ? maximum.y : minimum.y,plane.normal.z>=0 ? maximum.z : minimum.z};
        if(plane.signedDistance(support)<0) return false;
    }
    return true;
}
CameraComponent::CameraComponent(std::string name) : Component(std::move(name)) {}
bool Frustum::intersectsTransformedAabb(Vec3 minimum,Vec3 maximum,const Mat4& model) const {
    if(!finite(minimum)||!finite(maximum)||minimum.x>maximum.x||minimum.y>maximum.y||minimum.z>maximum.z)
        throw std::invalid_argument("Invalid bounds");
    // Local-space plane support handles rotation, shear and negative scale.
    for(const auto& plane:planes) {
        const auto n=plane.normal;
        std::array<double,4> p{};
        for(int c=0;c<4;++c) p[c]=double(n.x)*model.m[c*4]+double(n.y)*model.m[c*4+1]+double(n.z)*model.m[c*4+2];
        p[3]+=plane.distance;
        const double x=p[0]*(p[0]>=0 ? maximum.x : minimum.x),y=p[1]*(p[1]>=0 ? maximum.y : minimum.y),z=p[2]*(p[2]>=0 ? maximum.z : minimum.z);
        const double distance=x+y+z+p[3],tolerance=1e-5*(1+std::abs(x)+std::abs(y)+std::abs(z)+std::abs(p[3]));
        if(std::isfinite(distance) && distance < -tolerance) return false;
    }
    return true;
}
CameraComponent::~CameraComponent() = default;
void CameraComponent::invalidateProjection() {
    projectionValid_=combinedValid_=inverseValid_=frustumValid_=false;
    ++revision_;
}
void CameraComponent::setPerspective(float fov,float nearDistance,float farDistance) {
    perspective(fov,1,nearDistance,farDistance); // Validate before committing any field.
    if(projectionMode_==ProjectionMode::Perspective && fieldOfView==fov && nearPlane==nearDistance && farPlane==farDistance) return;
    projectionMode_=ProjectionMode::Perspective;fieldOfView=fov;nearPlane=nearDistance;farPlane=farDistance;
    invalidateProjection();
}
void CameraComponent::setOrthographic(float height,float nearDistance,float farDistance) {
    orthographic(height,1,nearDistance,farDistance);
    if(projectionMode_==ProjectionMode::Orthographic && orthographicHeight_==height && nearPlane==nearDistance && farPlane==farDistance) return;
    projectionMode_=ProjectionMode::Orthographic;orthographicHeight_=height;nearPlane=nearDistance;farPlane=farDistance;
    invalidateProjection();
}
void CameraComponent::setViewport(Viewport v) {
    if(!(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width) && std::isfinite(v.height)
        && v.x>=0 && v.y>=0 && v.width>0 && v.height>0 && v.width<=1-v.x && v.height<=1-v.y))
        throw std::invalid_argument("Viewport must fit within normalized render target");
    if(viewport_.x==v.x && viewport_.y==v.y && viewport_.width==v.width && viewport_.height==v.height) return;
    viewport_=v;++revision_;
}
PixelViewport CameraComponent::pixelViewport(float width,float height) const {
    validatePixelViewport({0,0,width,height});
    PixelViewport out{viewport_.x*width,viewport_.y*height,viewport_.width*width,viewport_.height*height};
    validatePixelViewport(out);return out;
}
void CameraComponent::setVisibilityMask(std::uint32_t mask) {
    if(mask!=visibilityMask_) {visibilityMask_=mask;++revision_;}
}
bool CameraComponent::active() const {
    return isActiveInHierarchy();
}
void CameraComponent::synchronize() const {
    const std::array lens={fieldOfView,nearPlane,farPlane};
    const bool lensChanged=!stateValid_ || lensSnapshot_!=lens;
    if(lensChanged) {
        if(projectionMode_==ProjectionMode::Perspective) perspective(fieldOfView,1,nearPlane,farPlane);
        else orthographic(orthographicHeight_,1,nearPlane,farPlane);
    }
    const auto world=worldMatrix();
    const bool worldChanged=!stateValid_ || world.m!=worldCache_.m;
    Mat4 nextView;
    if(worldChanged) {
        for(float value:world.m) if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite camera transform");
        nextView=inverseAffine(world);
        for(float value:nextView.m) if(!std::isfinite(value)) throw std::invalid_argument("Invalid camera inverse");
    }
    const bool enabledNow=active();
    if(lensChanged || worldChanged || enabledNow!=activeSnapshot_) ++revision_;
    if(lensChanged) {lensSnapshot_=lens;projectionValid_=false;}
    if(worldChanged) {worldCache_=world;viewCache_=nextView;}
    if(lensChanged || worldChanged) combinedValid_=inverseValid_=frustumValid_=false;
    activeSnapshot_=enabledNow;stateValid_=true;
}
std::uint64_t CameraComponent::revision() const { synchronize();return revision_; }
const Mat4& CameraComponent::view() const { synchronize();return viewCache_; }
const Mat4& CameraComponent::projection(float aspect) const {
    if(!std::isfinite(aspect) || aspect<=0) throw std::invalid_argument("Invalid camera aspect ratio");
    synchronize();
    if(!projectionValid_ || aspectCache_!=aspect) {
        const auto next=projectionMode_==ProjectionMode::Perspective ? perspective(fieldOfView,aspect,nearPlane,farPlane)
            : orthographic(orthographicHeight_,aspect,nearPlane,farPlane);
        for(float value:next.m) if(!std::isfinite(value)) throw std::invalid_argument("Unrepresentable camera projection");
        if(aspectCache_!=aspect) ++revision_;
        projectionCache_=next;aspectCache_=aspect;projectionValid_=true;
        combinedValid_=inverseValid_=frustumValid_=false;
    }
    return projectionCache_;
}
const Mat4& CameraComponent::viewProjection(float aspect) const {
    projection(aspect);
    if(!combinedValid_) {viewProjectionCache_=projectionCache_*viewCache_;combinedValid_=true;}
    return viewProjectionCache_;
}
const Mat4& CameraComponent::inverseViewProjection(float aspect) const {
    viewProjection(aspect);
    if(!inverseValid_) {inverseViewProjectionCache_=inverse(viewProjectionCache_);inverseValid_=true;}
    return inverseViewProjectionCache_;
}
Frustum Frustum::fromMatrix(const Mat4& matrix) {
    const auto& m=matrix.m;Frustum next;
    for(int i=0;i<6;++i) {
        const int row=i/2;const float sign=i%2 ? -1.0f : 1.0f;
        std::array<float,4> p{};
        for(int c=0;c<4;++c) p[c]=(i==4 ? 0 : m[c*4+3])+sign*m[c*4+row];
        const float length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
        if(!std::isfinite(length) || length<=0) throw std::invalid_argument("Degenerate frustum");
        next.planes[i]={{p[0]/length,p[1]/length,p[2]/length},p[3]/length};
    }
    return next;
}
const Frustum& CameraComponent::frustum(float aspect) const {
    const auto& matrix=viewProjection(aspect);
    if(!frustumValid_) { frustumCache_=Frustum::fromMatrix(matrix);frustumValid_=true; }
    return frustumCache_;
}
Vec3 CameraComponent::worldPosition() const { synchronize();return transformPoint(worldCache_,{}); }
Vec3 CameraComponent::forward() const { synchronize();return unit(transformDirection(worldCache_,{0,0,-1})); }
Vec3 CameraComponent::right() const { synchronize();return unit(transformDirection(worldCache_,{1,0,0})); }
Vec3 CameraComponent::up() const { synchronize();return unit(transformDirection(worldCache_,{0,1,0})); }
void CameraComponent::lookAt(Vec3 target,Vec3 worldUp) {
    const auto direction=unit(target-worldPosition());worldUp=unit(worldUp);
    if(dot(cross(direction,worldUp),cross(direction,worldUp))<1e-12f)
        throw std::invalid_argument("Camera target and up are parallel");
    const auto parentInverse=parent() ? inverseAffine(parent()->worldMatrix()) : Mat4::identity();
    // Solve in parent space. Preserve position/scale and align local -Z with the target.
    const auto z=unit(transformDirection(parentInverse,direction))*(transform.scale.z<0 ? 1.0f : -1.0f);
    const auto desiredUp=unit(transformDirection(parentInverse,worldUp))*(transform.scale.y<0 ? -1.0f : 1.0f);
    const auto x=unit(cross(desiredUp,z));const auto y=cross(z,x);
    Vec3 rotation;
    rotation.y=std::atan2(-x.z,std::hypot(x.x,x.y));
    if(std::hypot(x.x,x.y)>1e-6f) {rotation.x=std::atan2(y.z,z.z);rotation.z=std::atan2(x.y,x.x);}
    else {rotation.x=std::atan2(-z.y,y.y);rotation.z=0;}
    transform.rotation=rotation;
}
ScreenPoint CameraComponent::worldToScreen(Vec3 point,PixelViewport viewport) const {
    validatePixelViewport(viewport);if(!finite(point)) throw std::invalid_argument("Invalid world point");
    const auto clip=homogeneous(viewProjection(viewport.width/viewport.height),point);
    const bool inFront=transformPoint(viewCache_,point).z<0;
    if(clip[3]==0) {
        const float nan=std::numeric_limits<float>::quiet_NaN();return {{nan,nan},nan,inFront,false};
    }
    const double x=clip[0]/clip[3],y=clip[1]/clip[3];
    double z=clip[2]/clip[3];
    // Roundoff at a clipping plane should not make its depth impossible to unproject.
    constexpr double tolerance=8*std::numeric_limits<float>::epsilon();
    if(z>=-tolerance && z<=1+tolerance) z=std::clamp(z,0.0,1.0);
    return {{float(viewport.x+(x+1)*0.5*viewport.width),float(viewport.y+(1-y)*0.5*viewport.height)},float(z),inFront,
        activeSnapshot_ && clip[3]>0 && x>=-1-tolerance && x<=1+tolerance && y>=-1-tolerance && y<=1+tolerance && z>=0 && z<=1};
}
Vec3 CameraComponent::screenToWorld(Vec2 pixel,float depth,PixelViewport viewport) const {
    validatePixelViewport(viewport);
    if(!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(depth) || depth<0 || depth>1)
        throw std::invalid_argument("Invalid screen coordinate or depth");
    const Vec3 ndc{2*(pixel.x-viewport.x)/viewport.width-1,1-2*(pixel.y-viewport.y)/viewport.height,depth};
    const auto p=homogeneous(inverseViewProjection(viewport.width/viewport.height),ndc);
    if(p[3]==0) throw std::invalid_argument("Screen point is at infinity");
    const Vec3 out{float(p[0]/p[3]),float(p[1]/p[3]),float(p[2]/p[3])};
    if(!finite(out)) throw std::invalid_argument("Unrepresentable world point");
    return out;
}
Ray CameraComponent::screenPointToRay(Vec2 pixel,PixelViewport viewport) const {
    const auto nearPoint=screenToWorld(pixel,0,viewport);
    if(projectionMode_==ProjectionMode::Perspective) {
        const auto eye=worldPosition();return {eye,unit(nearPoint-eye)};
    }
    return {nearPoint,forward()};
}
} // namespace engine
