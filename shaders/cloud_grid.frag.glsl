#version 300 es
precision highp float;
#define VOLUMETRIC_CLOUDS
uniform vec4 skyClouds,skySun,skyHorizon,cloudEye;
uniform mat4 cloudInverseVp;
vec3 cloudLayer(vec3 d,vec3 background) {return background;}
// CLOUD_VOLUME
out vec4 outputColor;
vec3 unprojectCloud(vec2 ndc,float depth) {
    vec4 p=cloudInverseVp*vec4(ndc,depth,1);return p.xyz/p.w;
}
void main() {
    vec2 pixel=gl_FragCoord.xy-vec2(64,0);
    float slice=floor(pixel.y/36.0)+1.0;
    vec2 ndc=vec2(pixel.x,pixel.y-(slice-1.0)*36.0)/vec2(64,36)*2.0-1.0;
    vec3 nearPoint=unprojectCloud(ndc,0.0),farPoint=unprojectCloud(ndc,1.0);
    vec3 offset=cloudEye.w>0.5?vec3(0):nearPoint-unprojectCloud(vec2(0),0.0);
    vec3 d=normalize(farPoint-nearPoint);
    float originY=cloudOrigin.y+offset.y,enter=0.0,leave=skyCloudLayer.w;
    if(abs(d.y)<0.00001) {
        if(originY<=skyCloudLayer.x || originY>=skyCloudLayer.x+skyCloudLayer.y) leave=0.0;
    } else {
        float a=(skyCloudLayer.x-originY)/d.y,b=(skyCloudLayer.x+skyCloudLayer.y-originY)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    if(leave<=enter || skyCloudLayer.z<=0.0) {outputColor=vec4(0,0,0,1);return;}
    int count=int(clamp(cloudShape.z,1.0,64.0));
    float ds=(leave-enter)/float(count),prefix=enter+(leave-enter)*slice/32.0;
    vec3 origin=vec3(cloudOrigin.x+offset.x*cloudOrigin.w,originY*cloudOrigin.w,cloudOrigin.z+offset.z*cloudOrigin.w);
    float day=smoothstep(-0.08,0.2,skySun.y);
    vec3 ambient=cloudAmbient(day);
    vec3 sunColor=cloudLightColor();
    float g=cloudShape.y,phase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*clamp(dot(d,skySun.xyz),-1.0,1.0),.001),1.5);
    vec3 radiance=vec3(0);float transmission=1.0;
    for(int step=0;step<64;++step) {
        float start=enter+float(step)*ds;
        if(step>=count || start>=prefix || transmission<.01) break;
        float distance=start+.5*ds,height=originY+d.y*distance;
        vec3 point=origin+d*(distance*cloudOrigin.w);
        float rho=cloudDensity(point,height);
        if(rho>.0001) {
            float t=exp(-min(80.0,rho*skyCloudLayer.z*min(ds,prefix-start)));
            vec3 source=(ambient+sunColor*(day*skySun.w*.08*phase*cloudSunlight(point,height)))*skyHorizon.w;
            radiance+=transmission*(1.0-t)*source;transmission*=t;
        }
    }
    outputColor=vec4(min(radiance,vec3(65504)),transmission);
}
