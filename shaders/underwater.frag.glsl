#version 300 es
precision highp float;
uniform bool hdrOutput;
vec3 hdrEncode(vec3 c){return hdrOutput?c:pow(c,vec3(1.0/2.2));}

uniform highp sampler2D sourceColor,sourceDepth;
uniform mat4 inverseViewProjection,worldToLocal;
uniform vec4 viewport,eye,minimum,maximum,absorptionStrength,colorDensity,options,caustics;
out vec4 outputColor;
vec3 point(vec2 pixel,float depth) {
    vec2 ndc=(pixel-viewport.xy)/viewport.zw*2.0-1.0;
    vec4 p=inverseViewProjection*vec4(ndc,depth,1);return p.xyz/p.w;
}
vec2 slab(float p,float d,float lo,float hi,vec2 interval) {
    if(abs(d)<1e-7) return p>=lo&&p<=hi ? interval : vec2(1,0);
    float a=(lo-p)/d,b=(hi-p)/d;return vec2(max(interval.x,min(a,b)),min(interval.y,max(a,b)));
}
float distanceInWater(vec3 start,vec3 end) {
    vec3 origin=(worldToLocal*vec4(start,1)).xyz,delta=(worldToLocal*vec4(end-start,0)).xyz;
    vec2 interval=slab(origin.x,delta.x,minimum.x,maximum.x,vec2(0,1));
    interval=slab(origin.z,delta.z,minimum.z,maximum.z,interval);
    if(abs(delta.y)<1e-7) {if(origin.y>0.0) return 0.0;}
    else if(delta.y>0.0) interval.y=min(interval.y,-origin.y/delta.y);
    else interval.x=max(interval.x,-origin.y/delta.y);
    return min(options.x,max(0.0,interval.y-interval.x)*length(end-start));
}
float causticPattern(vec2 p,float t) {
    vec2 cell=floor(p),f=fract(p),nearest=vec2(8);
    for(int y=-1;y<=1;y++) {for(int x=-1;x<=1;x++) {
        vec2 g=vec2(x,y),seed=cell+g;
        vec2 phase=vec2(dot(seed,vec2(127.1,311.7)),dot(seed,vec2(269.5,183.3)));
        vec2 offset=.5+.32*sin(phase+t*vec2(.8,1.1));float d=length(g+offset-f);
        if(d<nearest.x) nearest=vec2(d,nearest.x);else nearest.y=min(nearest.y,d);
    }}
    return 1.0-smoothstep(.025,.14,nearest.y-nearest.x);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 source=texelFetch(sourceColor,pixel,0);
    float depth=texelFetch(sourceDepth,pixel,0).r;vec3 end=point(gl_FragCoord.xy,depth);
    vec3 surface=cross(dFdx(end),dFdy(end)),local=(worldToLocal*vec4(end,1)).xyz;
    vec3 planeNormal=vec3(worldToLocal[0].y,worldToLocal[1].y,worldToLocal[2].y);
    float planeLength=length(planeNormal),below=-local.y/planeLength;
    float facing=abs(dot(surface,planeNormal))/(max(length(surface),1e-8)*planeLength),light=0.0;
    if(caustics.x>0.0 && depth<.999999 && local.y < -options.w &&
        local.x>=minimum.x && local.x<=maximum.x && local.z>=minimum.z && local.z<=maximum.z) {
        vec2 units=vec2(length(vec3(worldToLocal[0].x,worldToLocal[1].x,worldToLocal[2].x)),
                        length(vec3(worldToLocal[0].z,worldToLocal[1].z,worldToLocal[2].z)));
        light=causticPattern(local.xz/units*caustics.y,caustics.z)*caustics.x*
            (1.0-smoothstep(0.0,caustics.w,below))*smoothstep(.15,.75,facing)*absorptionStrength.w;
    }
    vec3 start=options.y>0.5 ? point(gl_FragCoord.xy,0.0) : eye.xyz;
    float path=distanceInWater(start,end)*absorptionStrength.w;
    vec3 attenuation=exp(-absorptionStrength.xyz*path);float transmission=exp(-colorDensity.w*path);
    vec3 linear=(hdrOutput?source.rgb:pow(source.rgb,vec3(2.2)))*(1.0+light)*attenuation*transmission+colorDensity.xyz*(1.0-transmission);
    outputColor=vec4(hdrEncode(linear),source.a);
}
