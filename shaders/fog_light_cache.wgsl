// FOG_FUNCTIONS
// FOG_FRAME
@group(0) @binding(0) var<uniform> frame:Frame;
@group(0) @binding(1) var shadowMaps:texture_depth_2d_array;
@group(0) @binding(2) var shadowSampler:sampler_comparison;
@group(0) @binding(4) var outputCache:texture_storage_3d<rgba16float,write>;
// FOG_VOLUME

@compute @workgroup_size(4,4,4)
fn fogLightCacheCompute(@builtin(global_invocation_id) invocation:vec3u) {
    let coordinates=vec3f(invocation)/63;
    var depths=vec4f(0);
    for(var slot=0u;slot<u32(frame.fog.volumeLighting.z);slot++) {
        let cache=frame.fog.lightCache[slot];
        let light=frame.lights[u32(cache.minimumIndex.w)];
        let local=mix(cache.minimumIndex.xyz,cache.maximum.xyz,coordinates);
        let point=light.positionType.xyz+fogLightBasis(light.directionRange.xyz)*local;
        let delta=light.positionType.xyz-point;
        let distance=length(delta);
        depths[slot]=min(80.0,fogDirectOpticalDepth(frame.fog,point,delta/max(distance,1e-6),distance));
    }
    textureStore(outputCache,vec3i(invocation),depths);
}
