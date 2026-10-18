#version 300 es
precision highp float;
layout(location=0) in vec3 position;
uniform bool sphereGeometry;
uniform mat4 clipFromDirection;
uniform vec4 skyRight,skyUp,skyForward,fogRight,fogUp;
out vec3 fogOffset;
out vec3 direction;
void main() {
    if(sphereGeometry) {
        gl_Position=clipFromDirection*vec4(position,1);
        gl_Position.z=gl_Position.w*0.9998;
        direction=skyForward.w>0.5 ? position : skyForward.xyz;
    } else {
        vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
        gl_Position=vec4(p[gl_VertexID],0,1);
        direction=skyForward.xyz+skyRight.xyz*(p[gl_VertexID].x*skyRight.w)+skyUp.xyz*(p[gl_VertexID].y*skyUp.w);
    }
    fogOffset=(fogRight.xyz*gl_Position.x+fogUp.xyz*gl_Position.y)/gl_Position.w;
}
