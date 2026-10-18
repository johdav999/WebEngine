#pragma once
#include "engine/route.hpp"
namespace engine {
inline std::shared_ptr<const RouteAsset> exampleRoute(){
    SplineData spline;spline.points={{{0,0,-8},{0,0,30},0},{{3,1,24},{6,0,30},.1f},{{9,0,56},{0,0,30},0}};
    SweepProfileData profile;profile.points={{-4,0},{4,0}};auto material=std::make_shared<Material>();material->name="Route asphalt";material->baseColor={.22f,.26f,.3f};material->roughness=.9f;profile.material=material;auto surface=std::make_shared<PhysicalSurface>();surface->id="route.asphalt";profile.surface=surface;
    RouteData route;route.spline=SplineAsset::create(std::move(spline));route.profile=SweepProfile::create(std::move(profile));route.spacing=.5f;route.collision=true;return RouteAsset::create(std::move(route));
}
}
