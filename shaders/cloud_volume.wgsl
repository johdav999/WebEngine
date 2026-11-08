// Sky-only single scattering through a world-height cloud slab.
override volumetricCloudsEnabled:bool=false;
fn cloudNoise(point:vec3f) -> vec2f {
    let node=fract(point)*64.0-0.5;
    let slice=u32(floor(node.z)+64.0)%64u;
    let next=(slice+1u)%64u;
    let tile=vec2f(f32(slice%8u),f32(slice/8u))*66.0;
    let nextTile=vec2f(f32(next%8u),f32(next/8u))*66.0;
    let a=textureSampleLevel(cloudNoiseAtlas,panoramaSampler,(tile+node.xy+1.5)/528.0,0).rg;
    let b=textureSampleLevel(cloudNoiseAtlas,panoramaSampler,(nextTile+node.xy+1.5)/528.0,0).rg;
    return mix(a,b,fract(node.z));
}
fn cloudDensity(point:vec3f,height:f32) -> f32 {
    let h=(height-sky.cloudLayer.x)/sky.cloudLayer.y;
    if(h<=0.0 || h>=1.0) {return 0.0;}
    let value=cloudNoise(point);
    let shape=smoothstep(1.0-sky.clouds.x,1.0-sky.clouds.x+0.18,
        value.x-sky.cloudShape.x*(1.0-value.y)*0.12);
    let profile=smoothstep(0.0,0.16,h)*(1.0-smoothstep(0.65,1.0,h));
    return shape*profile*sky.clouds.y;
}
fn cloudSunlight(point:vec3f,height:f32) -> f32 {
    if(sky.sun.y<=0.01) {return 0.0;}
    let distance=min(sky.cloudLayer.w,max(0.0,(sky.cloudLayer.x+sky.cloudLayer.y-height)/sky.sun.y));
    var optical=0.0;
    for(var j=0u;j<4u;j++) {
        let a=f32(j)/4.0;let b=f32(j+1u)/4.0;
        let s=(a*a+b*b)*0.5*distance;
        optical+=cloudDensity(point+sky.sun.xyz*(s*sky.cloudOrigin.w),height+sky.sun.y*s)*((b*b-a*a)*distance);
    }
    return exp(-min(80.0,optical*sky.cloudLayer.z));
}
fn volumetricClouds(d:vec3f,offset:vec3f,background:vec3f) -> vec3f {
    if(!volumetricCloudsEnabled || sky.cloudShape.z<1.0) {
        return cloudLayer(d,background);
    }
    if(sky.clouds.x<=0.0 || sky.clouds.y<=0.0 || sky.cloudLayer.z<=0.0) {return background;}
    let originY=sky.cloudOrigin.y+offset.y;
    var enter=0.0;var leave=sky.cloudLayer.w;
    if(abs(d.y)<0.00001) {
        if(originY<=sky.cloudLayer.x || originY>=sky.cloudLayer.x+sky.cloudLayer.y) {return background;}
    } else {
        let a=(sky.cloudLayer.x-originY)/d.y;
        let b=(sky.cloudLayer.x+sky.cloudLayer.y-originY)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    if(leave<=enter) {return background;}
    let steps=u32(clamp(sky.cloudShape.z,1.0,64.0));let ds=(leave-enter)/f32(steps);
    let day=smoothstep(-0.08,0.2,sky.sun.y);
    let ambient=mix(vec3f(0.012,0.018,0.035),vec3f(0.18,0.22,0.29),day);
    let sunColor=mix(vec3f(1.0,0.45,0.22),vec3f(1.0,0.98,0.92),smoothstep(0.0,0.35,sky.sun.y));
    let g=sky.cloudShape.y;let mu=clamp(dot(d,sky.sun.xyz),-1.0,1.0);
    let phase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*mu,0.001),1.5);
    let origin=vec3f(sky.cloudOrigin.x+offset.x*sky.cloudOrigin.w,originY*sky.cloudOrigin.w,
        sky.cloudOrigin.z+offset.z*sky.cloudOrigin.w);
    var transmission=1.0;var radiance=vec3f(0.0);
    for(var i=0u;i<64u;i++) {
        if(i>=steps || transmission<0.01) {break;}
        let distance=enter+(f32(i)+0.5)*ds;
        let height=originY+d.y*distance;
        let point=origin+d*(distance*sky.cloudOrigin.w);
        let rho=cloudDensity(point,height);
        if(rho>0.0001) {
            let t=exp(-min(80.0,rho*sky.cloudLayer.z*ds));
            let source=ambient+sunColor*(day*sky.sun.w*0.08*phase*cloudSunlight(point,height));
            radiance+=transmission*(1.0-t)*source;transmission*=t;
        }
    }
    return radiance+transmission*background;
}
