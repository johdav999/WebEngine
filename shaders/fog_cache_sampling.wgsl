// FOG_CACHE_SAMPLING_BEGIN
fn fogCacheOpticalDepth(point:vec3f,slot:u32) -> f32 {
    // Lattice nodes include the domain faces. Sample their texture centers.
    let coordinates=(clamp(point,vec3f(0),vec3f(1))*63+0.5)/64;
    return textureSampleLevel(fogLightCache,fogLightCacheSampler,coordinates,0)[slot];
}
// FOG_CACHE_SAMPLING_END
