#version 300 es
precision highp float;
#define VOLUMETRIC_CLOUDS
uniform vec4 skyClouds,skySun,cloudEye,cloudShadow;
vec3 cloudLayer(vec3 d,vec3 background) {return background;}
// CLOUD_VOLUME
void main() {
    vec2 entry=cloudShadow.xy+(gl_FragCoord.xy*cloudShadow.w-.5)*(2.0*cloudShadow.z);
    vec3 origin=vec3(cloudOrigin.x+(entry.x-cloudEye.x)*cloudOrigin.w,skyCloudLayer.x*cloudOrigin.w,
        cloudOrigin.z+(entry.y-cloudEye.z)*cloudOrigin.w);
    float distance=min(skyCloudLayer.w,skyCloudLayer.y/skySun.y),optical=0.0;
    for(int i=0;i<16;++i) {
        float s=(float(i)+.5)*distance/16.0;
        optical+=cloudDensity(origin+skySun.xyz*(s*cloudOrigin.w),skyCloudLayer.x+skySun.y*s)*(distance/16.0);
    }
    gl_FragDepth=exp(-min(80.0,optical*skyCloudLayer.z));
}
