// FOG_FUNCTIONS
struct Camera { viewProjection: mat4x4f, right: vec4f, up: vec4f, fog:Fog, inverseVp:mat4x4f, viewport:vec4f, forward:vec4f };
@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var opaqueDepth: texture_2d<f32>;
@group(1) @binding(0) var colorSampler: sampler;
@group(1) @binding(1) var colorTexture: texture_2d<f32>;
struct Output {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(2) world:vec3f,
    @location(3) @interpolate(flat) additive:f32,
    @location(4) @interpolate(flat) softDistance:f32,
    @location(5) @interpolate(flat) ribbon:f32,
    @location(1) color: vec4f,
};
@vertex fn particleVertex(@builtin(vertex_index) vertex: u32,
    @location(0) positionSize: vec4f, @location(1) color: vec4f,
    @location(2) ageRotation: vec4f, @location(3) atlas: vec4f, @location(4) parameters:vec4f, @location(5) planeRight:vec4f, @location(6) planeUp:vec4f) -> Output {
    let corners=array<vec2f,6>(vec2f(-0.5,-0.5),vec2f(0.5,-0.5),vec2f(-0.5,0.5),
        vec2f(-0.5,0.5),vec2f(0.5,-0.5),vec2f(0.5,0.5));
    let corner=corners[vertex];
    let shaped=corner*vec2f(positionSize.w,positionSize.w+ageRotation.w);
    let c=cos(ageRotation.y);let s=sin(ageRotation.y);
    let rotated=vec2f(c*shaped.x-s*shaped.y,s*shaped.x+c*shaped.y);
    var out: Output;
    let right=select(camera.right.xyz,planeRight.xyz,parameters.y>0.5);
    let up=select(camera.up.xyz,planeUp.xyz,parameters.y>0.5);
    out.world=positionSize.xyz+right*rotated.x+up*rotated.y;out.additive=ageRotation.z;
    out.clip=camera.viewProjection*vec4f(out.world,1);
    let age=clamp(ageRotation.x,0,1);
    let phase=select(fract(age*atlas.w),1.0,age>=1.0);
    let frame=min(u32(phase*atlas.z),u32(atlas.z)-1u);
    let grid=vec2f(atlas.x,atlas.y);
    let cell=vec2f(f32(frame%u32(atlas.x)),f32(frame/u32(atlas.x)));
    // Top-left frame order; half-texel inset and mip level zero prevent neighboring
    // atlas cells from bleeding into this frame, including transparent edges.
    let localUV=vec2f(corner.x+0.5,0.5-corner.y);
    let inset=0.5/vec2f(textureDimensions(colorTexture));
    out.uv=(cell+localUV)/grid+inset*(vec2f(1)-2*localUV);
    out.ribbon=0;out.softDistance=parameters.x;out.color=color;return out;
}
@vertex fn ribbonVertex(@location(0) positionAdditive:vec4f,@location(1) color:vec4f,@location(2) uvSoft:vec4f)->Output {
    var out:Output;out.world=positionAdditive.xyz;out.clip=camera.viewProjection*vec4f(out.world,1);
    out.uv=uvSoft.xy;out.softDistance=uvSoft.z;out.additive=positionAdditive.w;out.color=color;out.ribbon=1;return out;
}
@fragment fn particleFragment(in: Output) -> @location(0) vec4f {
    let uv=select(in.uv,vec2f(fract(in.uv.x),in.uv.y),in.ribbon>0.5);
    let texel=textureSampleLevel(colorTexture,colorSampler,uv,0);
    var fade=1.0;
    if(in.softDistance>0) {
        let pixel=clamp(in.clip.xy,camera.viewport.xy+vec2f(0.5),camera.viewport.xy+camera.viewport.zw-vec2f(0.5));
        let depth=textureLoad(opaqueDepth,vec2i(pixel),0).r;
        if(depth<0.999999) {
            let ndc=(pixel-camera.viewport.xy)/camera.viewport.zw*vec2f(2,-2)+vec2f(-1,1);
            let point=camera.inverseVp*vec4f(ndc,depth,1);
            let gap=dot(point.xyz/point.w-in.world,camera.forward.xyz);
            fade=clamp(gap/in.softDistance,0,1);
        }
    }
    let alpha=texel.a*in.color.a*fade;
    let linear=texel.rgb*in.color.rgb;
    let mapped=linear/(vec3f(1)+linear);
    // Match the current mesh renderer's display conversion, premultiply for blending.
    return vec4f(pow(fogApply(camera.fog,mapped,in.world,in.additive>0.5),vec3f(1.0/2.2))*alpha,alpha);
}
