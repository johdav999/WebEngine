#ifdef VOLUMETRIC_CLOUDS
uniform vec4 skyCloudLayer,cloudShape,cloudOrigin;
uniform highp sampler2D cloudNoiseAtlas;
vec2 cloudNoise(vec3 point) {
    vec3 node=fract(point)*64.0-0.5;
    int slice=int(floor(node.z)+64.0)%64,next=(slice+1)%64;
    vec2 tile=vec2(float(slice%8),float(slice/8))*66.0;
    vec2 nextTile=vec2(float(next%8),float(next/8))*66.0;
    vec2 a=textureLod(cloudNoiseAtlas,(tile+node.xy+1.5)/528.0,0.0).rg;
    vec2 b=textureLod(cloudNoiseAtlas,(nextTile+node.xy+1.5)/528.0,0.0).rg;
    return mix(a,b,fract(node.z));
}
float cloudDensity(vec3 point,float height) {
    float h=(height-skyCloudLayer.x)/skyCloudLayer.y;
    if(h<=0.0 || h>=1.0) return 0.0;
    vec2 value=cloudNoise(point);
    float shape=smoothstep(1.0-skyClouds.x,1.0-skyClouds.x+0.18,
        value.x-cloudShape.x*(1.0-value.y)*0.12);
    float profile=smoothstep(0.0,0.16,h)*(1.0-smoothstep(0.65,1.0,h));
    return shape*profile*skyClouds.y;
}
float cloudSunlight(vec3 point,float height) {
    if(skySun.y<=0.01) return 0.0;
    float distance=min(skyCloudLayer.w,max(0.0,(skyCloudLayer.x+skyCloudLayer.y-height)/skySun.y));
    float optical=0.0;
    for(int j=0;j<4;j++) {
        float a=float(j)/4.0,b=float(j+1)/4.0;
        float s=(a*a+b*b)*0.5*distance;
        optical+=cloudDensity(point+skySun.xyz*(s*cloudOrigin.w),height+skySun.y*s)*((b*b-a*a)*distance);
    }
    return exp(-min(80.0,optical*skyCloudLayer.z));
}
vec3 volumetricClouds(vec3 d,vec3 offset,vec3 background) {
    if(cloudShape.z<1.0) return cloudLayer(d,background);
    if(skyClouds.x<=0.0 || skyClouds.y<=0.0 || skyCloudLayer.z<=0.0) return background;
    float originY=cloudOrigin.y+offset.y;
    float enter=0.0,leave=skyCloudLayer.w;
    if(abs(d.y)<0.00001) {
        if(originY<=skyCloudLayer.x || originY>=skyCloudLayer.x+skyCloudLayer.y) return background;
    } else {
        float a=(skyCloudLayer.x-originY)/d.y,b=(skyCloudLayer.x+skyCloudLayer.y-originY)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    if(leave<=enter) return background;
    int steps=int(clamp(cloudShape.z,1.0,64.0));
    float ds=(leave-enter)/float(steps);
    float day=smoothstep(-0.08,0.2,skySun.y);
    vec3 ambient=mix(vec3(0.012,0.018,0.035),vec3(0.18,0.22,0.29),day);
    vec3 sunColor=mix(vec3(1.0,0.45,0.22),vec3(1.0,0.98,0.92),smoothstep(0.0,0.35,skySun.y));
    float g=cloudShape.y,mu=clamp(dot(d,skySun.xyz),-1.0,1.0);
    float phase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*mu,0.001),1.5);
    vec3 origin=vec3(cloudOrigin.x+offset.x*cloudOrigin.w,originY*cloudOrigin.w,
        cloudOrigin.z+offset.z*cloudOrigin.w);
    float transmission=1.0;
    vec3 radiance=vec3(0.0);
    for(int i=0;i<64;i++) {
        if(i>=steps || transmission<0.01) break;
        float distance=enter+(float(i)+0.5)*ds;
        float height=originY+d.y*distance;
        vec3 point=origin+d*(distance*cloudOrigin.w);
        float rho=cloudDensity(point,height);
        if(rho>0.0001) {
            float t=exp(-min(80.0,rho*skyCloudLayer.z*ds));
            vec3 source=ambient+sunColor*(day*skySun.w*0.08*phase*cloudSunlight(point,height));
            radiance+=transmission*(1.0-t)*source;transmission*=t;
        }
    }
    return radiance+transmission*background;
}
#endif
