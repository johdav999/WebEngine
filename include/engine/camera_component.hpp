#pragma once
#include "engine/component.hpp"

namespace engine {
enum class ProjectionMode { Perspective, Orthographic };
// Top-left origin. Viewport is normalized; PixelViewport uses render-target pixels.
struct Viewport { float x=0,y=0,width=1,height=1; };
struct PixelViewport { float x=0,y=0,width=1,height=1; };
struct ScreenPoint {
    Vec2 pixel;
    float depth=0; // WebGPU [0,1] between clipping planes.
    bool inFront=false,visible=false;
};
struct Ray { Vec3 origin,direction; };
struct Plane {
    Vec3 normal;
    float distance=0;
    float signedDistance(Vec3 point) const { return dot(normal,point)+distance; }
};
struct Frustum {
    static Frustum fromMatrix(const Mat4& viewProjection);
    // Inward unit normals: left, right, bottom, top, near, far.
    std::array<Plane,6> planes;
    bool containsPoint(Vec3 point) const;
    bool intersectsSphere(Vec3 center,float radius) const;
    bool intersectsAabb(Vec3 minimum,Vec3 maximum) const;
    bool intersectsTransformedAabb(Vec3 minimum,Vec3 maximum,const Mat4& model) const;
};
class CameraComponent : public Component {
public:
    explicit CameraComponent(std::string name = "Node");
    ~CameraComponent() override;
    std::string_view typeName() const noexcept override { return "CameraComponent"; }
    // Legacy direct writes remain supported and validated when queried. Prefer atomic setters.
    float fieldOfView=pi/3, nearPlane=0.1f, farPlane=100;
    void setPerspective(float verticalFov,float nearDistance,float farDistance);
    void setOrthographic(float height,float nearDistance,float farDistance);
    ProjectionMode projectionMode() const { return projectionMode_; }
    float verticalFov() const { return fieldOfView; }
    float orthographicHeight() const { return orthographicHeight_; }
    void setViewport(Viewport viewport);
    Viewport viewport() const { return viewport_; }
    PixelViewport pixelViewport(float targetWidth,float targetHeight) const;
    void setVisibilityMask(std::uint32_t mask);
    std::uint32_t visibilityMask() const { return visibilityMask_; }
    bool active() const; // Includes inherited enabled state.
    std::uint64_t revision() const; // Refreshes direct lens/transform/ancestor changes.
    const Mat4& view() const;
    const Mat4& projection(float aspect) const;
    const Mat4& viewProjection(float aspect) const;
    const Mat4& inverseViewProjection(float aspect) const;
    const Frustum& frustum(float aspect) const;
    void lookAt(Vec3 worldTarget,Vec3 worldUp={0,1,0});
    Vec3 worldPosition() const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;
    // Pass the actual pixel rectangle (e.g. pixelViewport). It is not applied twice.
    ScreenPoint worldToScreen(Vec3 point,PixelViewport viewport) const;
    Vec3 screenToWorld(Vec2 pixel,float depth,PixelViewport viewport) const;
    // Perspective rays start at the eye; orthographic rays start on the near plane.
    Ray screenPointToRay(Vec2 pixel,PixelViewport viewport) const;
private:
    void synchronize() const;
    void invalidateProjection();
    ProjectionMode projectionMode_=ProjectionMode::Perspective;
    float orthographicHeight_=10;
    Viewport viewport_;
    std::uint32_t visibilityMask_=0xffffffffu;
    mutable std::uint64_t revision_=0;
    mutable std::array<float,3> lensSnapshot_{};
    mutable Mat4 worldCache_,viewCache_,projectionCache_,viewProjectionCache_,inverseViewProjectionCache_;
    mutable Frustum frustumCache_;
    mutable float aspectCache_=0;
    mutable bool stateValid_=false,activeSnapshot_=false,projectionValid_=false;
    mutable bool combinedValid_=false,inverseValid_=false,frustumValid_=false;
};
} // namespace engine
