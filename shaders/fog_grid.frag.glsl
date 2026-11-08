#version 300 es
precision highp float;
// FOG_FUNCTIONS
// FOG_VOLUME
uniform mat4 fogInverseViewProjection;
out vec4 outputColor;
void main() {
    vec2 pixel=gl_FragCoord.xy;
    float slice=floor(pixel.y/36.0);
    vec2 uv=vec2(pixel.x,pixel.y-slice*36.0)/vec2(64,36);
    vec4 unprojected=fogInverseViewProjection*vec4(uv*2.0-1.0,1,1);
    vec3 endpoint=unprojected.xyz/unprojected.w;
    vec3 origin=fogOrigin(fog,endpoint);
    vec3 direction=normalize(endpoint-origin);
    vec2 slab=fogRayRange(origin,direction,fog.volumeMaxDistance.w);
    if(slab.x>=slab.y) {outputColor=vec4(0,0,0,1);return;}
    int substeps=int(ceil(fog.volumeMinSteps.w/16.0));
    int steps=(int(slice)+1)*substeps;
    float ds=(slab.y-slab.x)/float(16*substeps);
    ivec4 lights=fogVolumeLightIndices();
    // Grid production is used only for a single box.
    vec2 ranges[4]=vec2[4](vec2(0),vec2(0),vec2(0),vec2(0));
    vec3 scatter=vec3(0);
    float transmission=1.0;
    for(int i=0;i<steps && transmission>0.0001;i++) {
        vec4 segment=fogVolumeStep(origin,direction,slab.x+float(i)*ds,ds,fog.height.x,lights,1,ranges);
        scatter+=transmission*segment.rgb;
        transmission*=segment.a;
    }
    outputColor=vec4(min(scatter,vec3(65504)),transmission);
}
