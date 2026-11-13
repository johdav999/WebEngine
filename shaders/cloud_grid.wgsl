// FOG_FUNCTIONS
struct Cloud {layer:vec4f,eye:vec4f,sun:vec4f,shadow:vec4f};
struct Sky {right:vec4f,up:vec4f,forward:vec4f,horizon:vec4f,zenith:vec4f,nadir:vec4f,clipFromDirection:mat4x4f,sun:vec4f,air:vec4f,clouds:vec4f,cloudWind:vec4f,fog:Fog,fogRight:vec4f,fogUp:vec4f,cloudLayer:vec4f,cloudShape:vec4f,cloudOrigin:vec4f,cloud:Cloud};
@group(0) @binding(0) var<uniform> sky:Sky;
@group(1) @binding(0) var panoramaSampler:sampler;
@group(1) @binding(2) var cloudNoiseAtlas:texture_2d<f32>;
fn cloudLayer(d:vec3f,bg:vec3f)->vec3f {return bg;}
// CLOUD_VOLUME

@group(2) @binding(0) var<storage,read_write> outputGrid:array<vec2u>;
@compute @workgroup_size(8,4)
fn cloudGridCompute(@builtin(global_invocation_id) id:vec3u) {
    let ndc=(vec2f(id.xy)+0.5)/vec2f(64,36)*vec2f(2,-2)+vec2f(-1,1);
    let offset=sky.fogRight.xyz*ndc.x+sky.fogUp.xyz*ndc.y;
    let d=normalize(sky.forward.xyz+sky.right.xyz*(ndc.x*sky.right.w)+sky.up.xyz*(ndc.y*sky.up.w));
    let originY=sky.cloudOrigin.y+offset.y;var enter=0.0;var leave=sky.cloudLayer.w;
    if(abs(d.y)<0.00001) {if(originY<=sky.cloudLayer.x || originY>=sky.cloudLayer.x+sky.cloudLayer.y) {leave=0;}}
    else {let a=(sky.cloudLayer.x-originY)/d.y;let b=(sky.cloudLayer.x+sky.cloudLayer.y-originY)/d.y;enter=max(0.0,min(a,b));leave=min(leave,max(a,b));}
    let count=u32(clamp(sky.cloudShape.z,1,64));let ds=max(0.0,leave-enter)/f32(count);
    let origin=vec3f(sky.cloudOrigin.x+offset.x*sky.cloudOrigin.w,originY*sky.cloudOrigin.w,sky.cloudOrigin.z+offset.z*sky.cloudOrigin.w);
    let day=smoothstep(-0.08,0.2,sky.sun.y);let ambient=cloudAmbient(day);
    let sunColor=cloudLightColor();
    let g=sky.cloudShape.y;let phase=(1-g*g)/pow(max(1+g*g-2*g*clamp(dot(d,sky.sun.xyz),-1,1),.001),1.5);
    var radiance=vec3f(0);var transmission=1.0;var slice=1u;
    for(var step=0u;step<64u && step<count;step++) {
        let distance=enter+(f32(step)+.5)*ds;let height=originY+d.y*distance;let point=origin+d*(distance*sky.cloudOrigin.w);
        let rho=cloudDensity(point,height);let sigma=rho*sky.cloudLayer.z;
        var source=vec3f(0);if(rho>0.0001 && transmission>=.01) {source=(ambient+sunColor*(day*sky.sun.w*.08*phase*cloudSunlight(point,height)))*sky.horizon.w;}
        // Publish every prefix using the same full-ray midpoint samples, including fractional intervals.
        loop {
            if(slice>32u || f32(slice)*f32(count)/32.0>f32(step+1u)+.00001) {break;}
            let fraction=clamp(f32(slice)*f32(count)/32.0-f32(step),0,1);
            let t=select(1.0,exp(-min(80.0,sigma*ds*fraction)),rho>0.0001 && transmission>=.01);
            let value=min(radiance+transmission*(1-t)*source,vec3f(65504));
            outputGrid[36864u+((slice-1u)*36u+id.y)*64u+id.x]=vec2u(pack2x16float(value.rg),pack2x16float(vec2f(value.b,transmission*t)));
            slice++;
        }
        let t=select(1.0,exp(-min(80.0,sigma*ds)),rho>0.0001 && transmission>=.01);radiance+=transmission*(1-t)*source;transmission*=t;
    }
}
