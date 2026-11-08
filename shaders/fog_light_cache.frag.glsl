#version 300 es
precision highp float;
#define FOG_LIGHT_CACHE_BUILD
// FOG_FUNCTIONS
// FOG_VOLUME
out vec4 outputColor;
uniform float fogCacheBaseY;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy)-ivec2(0,int(fogCacheBaseY));
    int slice=(pixel.y/64)*8+pixel.x/64;
    vec3 unit=vec3(float(pixel.x%64),float(pixel.y%64),float(slice))/63.0;
    vec4 opticalDepth=vec4(0);
    for(int slot=0;slot<int(fog.volumeLighting.z);++slot) {
        FogLightCacheData cache=fog.lightCache[slot];
        int index=int(cache.minimumIndex.w);
        vec3 p=lightPosition[index].xyz+fogLightBasis(lightDirection[index].xyz)*mix(cache.minimumIndex.xyz,cache.maximum.xyz,unit);
        vec3 delta=lightPosition[index].xyz-p;
        float distance=length(delta);
        if(distance>1e-6) opticalDepth[slot]=fogLightOpticalDepth(p,delta/distance,distance);
    }
    outputColor=min(opticalDepth,vec4(80));
}
