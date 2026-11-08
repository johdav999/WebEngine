// FOG_FUNCTIONS
// FOG_FRAME
@group(0) @binding(0) var<uniform> frame:Frame;
@group(0) @binding(1) var shadowMaps:texture_depth_2d_array;
@group(0) @binding(2) var shadowSampler:sampler_comparison;
@group(0) @binding(3) var<storage,read_write> outputGrid:array<vec2u>;
@group(0) @binding(4) var fogLightCache:texture_3d<f32>;
@group(0) @binding(5) var fogLightCacheSampler:sampler;
// FOG_VOLUME

@compute @workgroup_size(8,4)
fn fogGridCompute(@builtin(global_invocation_id) invocation:vec3u) {
    let uv=(vec2f(invocation.xy)+0.5)/vec2f(64,36);
    let unprojected=frame.inverseViewProjection*vec4f(uv*vec2f(2,-2)+vec2f(-1,1),1,1);
    let endpoint=unprojected.xyz/unprojected.w;
    let f=frame.fog;
    let origin=fogOrigin(f,endpoint);
    let direction=normalize(endpoint-origin);
    let slab=fogRayRange(f,origin,direction,f.volumeMaxDistance.w);
    let substeps=u32(ceil(f.volumeMinSteps.w/16));
    let ds=max(0.0,slab.y-slab.x)/f32(16u*substeps);
    let lights=fogVolumeLightIndices();
    // Grid production is used only for a single box.
    let ranges=array<vec2f,4>();
    var scatter=vec3f(0);
    var transmission=1.0;
    for(var slice=0u;slice<16u;slice++) {
        for(var j=0u;j<substeps && ds>0 && transmission>0.0001;j++) {
            let offset=slab.x+f32(slice*substeps+j)*ds;
            let segment=fogVolumeStep(f,origin,direction,offset,ds,f.height.x,lights,1u,ranges);
            scatter+=transmission*segment.rgb;
            transmission*=segment.a;
        }
        let rgb=min(scatter,vec3f(65504));
        outputGrid[(slice*36u+invocation.y)*64u+invocation.x]=vec2u(pack2x16float(rgb.rg),pack2x16float(vec2f(rgb.b,transmission)));
    }
}
