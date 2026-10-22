#version 300 es
precision highp float;
uniform sampler2D sourceColor,bloomColor;
uniform vec4 params,grade;
uniform bool extractBloom;
out vec4 outputColor;
void main() {
    ivec2 size=textureSize(sourceColor,0);
    if(extractBloom) {
        vec3 sum=vec3(0);
        for(int y=0;y<4;y++)for(int x=0;x<4;x++) {
            vec3 c=texelFetch(sourceColor,clamp(ivec2(gl_FragCoord.xy)*4+ivec2(x,y),ivec2(0),size-1),0).rgb;
            float peak=max(max(c.x,c.y),c.z);sum+=c*(max(peak-params.z,0.0)/max(peak,1e-5));
        }
        outputColor=vec4(sum/16.0,1);return;
    }
    vec2 uv=gl_FragCoord.xy/vec2(size);vec3 glow=vec3(0);float weight=0.0;
    if(params.y>0.0) {
        for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++) {
            float w=float((3-abs(x))*(3-abs(y)));weight+=w;
            glow+=texture(bloomColor,uv+vec2(x,y)*2.0/vec2(textureSize(bloomColor,0))).rgb*w;
        }glow/=weight;
    }
    vec3 c=max(texelFetch(sourceColor,ivec2(gl_FragCoord.xy),0).rgb+glow*params.y,vec3(0))*params.x;
    c=c/(vec3(1)+c);c=mix(vec3(dot(c,vec3(.2126,.7152,.0722))),c,grade.x);
    c=clamp((c-vec3(.18))*grade.y+vec3(.18),vec3(0),vec3(1));
    outputColor=vec4(mix(12.92*c,1.055*pow(c,vec3(1.0/2.4))-.055,greaterThan(c,vec3(.0031308))),1);
}
