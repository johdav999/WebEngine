override fogLightCacheEnabled:bool=false;
fn fogVolumeLightEligible(index:i32) -> bool {
    let light=frame.lights[u32(index)];
    return light.colorIntensity.w>0 && (light.positionType.w<0.5 || light.positionType.w>1.5);
}
fn fogVolumeLightIndices() -> vec4i {
    // Prefer mapped lights, counting a light with multiple cascades only once.
    let budget=clamp(i32(frame.fog.volumeLighting.y),1,4);
    let lightCount=i32(frame.ambientCount.w);
    var indices=vec4i(-1);
    var count=0;
    for(var j=0u;j<4u;j++) {
        let shadow=frame.shadows[j];
        let index=i32(shadow.selection.x);
        if(shadow.options.x>=0 && index>=0 && index<lightCount && fogVolumeLightEligible(index) && !any(indices==vec4i(index))) {
            indices[count]=index;
            count++;
            if(count==budget) {return indices;}
        }
    }
    for(var j=0;j<lightCount;j++) {
        if(fogVolumeLightEligible(j) && !any(indices==vec4i(j))) {
            indices[count]=j;
            count++;
            if(count==budget) {return indices;}
        }
    }
    return indices;
}
// A negative result marks samples outside the map projection.
fn fogShadowMap(shadow:Shadow,p:vec3f,filtered:bool) -> f32 {
    let clip=shadow.viewProjection*vec4f(p,1);
    if(clip.w<=0) {return -1;}
    let ndc=clip.xyz/clip.w;
    let uv=ndc.xy*vec2f(0.5,-0.5)+0.5;
    if(any(uv<vec2f(0)) || any(uv>vec2f(1)) || ndc.z<0 || ndc.z>1) {return -1;}
    let layer=i32(shadow.options.x);
    let reference=ndc.z-shadow.options.y;
    let center=textureSampleCompareLevel(shadowMaps,shadowSampler,uv,layer,reference);
    if(!filtered) {return center;}
    // Fixed map-space offsets stay coherent as the view ray moves.
    let x=vec2f(shadow.options.w,0);
    let y=x.yx;
    return (2*center
        +textureSampleCompareLevel(shadowMaps,shadowSampler,uv+x,layer,reference)
        +textureSampleCompareLevel(shadowMaps,shadowSampler,uv-x,layer,reference)
        +textureSampleCompareLevel(shadowMaps,shadowSampler,uv+y,layer,reference)
        +textureSampleCompareLevel(shadowMaps,shadowSampler,uv-y,layer,reference))/6;
}
fn fogVolumeShadow(index:i32,p:vec3f) -> f32 {
    let depth=dot(p-frame.fog.eye.xyz,frame.fog.forward.xyz);
    let filtered=(i32(frame.fog.volumeOptions.w)&2)!=0;
    var primary=1.0;
    var nextWeight=0.0;
    for(var j=0u;j<4u;j++) {
        let shadow=frame.shadows[j];
        if(shadow.options.x<0 || i32(shadow.selection.x)!=index || (shadow.selection.w>0.5 && depth>shadow.selection.z)) {continue;}
        let value=fogShadowMap(shadow,p,filtered);
        if(nextWeight>0) {
            // Missing coverage in the adjacent map leaves the primary intact.
            return select(primary,mix(primary,value,nextWeight),value>=0);
        }
        if(value<0) {continue;}
        if(filtered && shadow.selection.w>0.5 && j+1u<4u) {
            let start=mix(shadow.selection.y,shadow.selection.z,0.9);
            let next=frame.shadows[j+1u];
            if(depth>start && next.options.x>=0 && next.selection.w>0.5 && i32(next.selection.x)==index && depth<=next.selection.z) {
                primary=value;
                nextWeight=smoothstep(start,shadow.selection.z,depth);
                continue;
            }
        }
        return value;
    }
    return primary;
}
fn fogVolumeCount(f:Fog) -> u32 {
    return clamp(u32(f.volumeLighting.x),1u,4u);
}
fn fogBox(f:Fog,index:u32) -> FogVolumeData {
    if(index==0u) {
        return FogVolumeData(vec4f(f.volumeMinSteps.xyz,f.volumeOptions.x),f.volumeMaxDistance,f.volumeColorDensity);
    }
    return f.additionalVolumes[index-1u];
}
fn fogBoxRange(box:FogVolumeData,origin:vec3f,direction:vec3f,limit:f32) -> vec2f {
    let lo=box.minimumFade.xyz;
    let hi=box.maximumDistance.xyz;
    // Clamp the interval first. Parallel axes use a finite reciprocal and a
    // separate containment test, including when the ray lies on a box face.
    let parallel=abs(direction)<vec3f(1e-8);
    let reciprocal=1/select(direction,vec3f(1),parallel);
    let a=(lo-origin)*reciprocal;
    let b=(hi-origin)*reciprocal;
    let slabEntry=select(min(a,b),vec3f(-1e20),parallel);
    let slabExit=select(max(a,b),vec3f(1e20),parallel);
    let enter=max(0.0,max(slabEntry.x,max(slabEntry.y,slabEntry.z)));
    let leave=min(limit,min(slabExit.x,min(slabExit.y,slabExit.z)));
    let outside=max(lo-origin,origin-hi);
    if(any(select(vec3f(0),outside,parallel)>vec3f(0))) {return vec2f(1,0);}
    return vec2f(enter,leave);
}
fn fogRayRange(f:Fog,origin:vec3f,direction:vec3f,limit:f32) -> vec2f {
    return fogBoxRange(fogBox(f,0u),origin,direction,limit);
}
fn fogDensityScale(box:FogVolumeData,p:vec3f) -> f32 {
    let edge=min(p-box.minimumFade.xyz,box.maximumDistance.xyz-p);
    return clamp(min(edge.x,min(edge.y,edge.z))/max(box.minimumFade.w,1e-6),0,1);
}
fn fogBoxOpticalDepth(box:FogVolumeData,samplePoint:vec3f,lightDirection:vec3f,lightDistance:f32) -> f32 {
    let slab=fogBoxRange(box,samplePoint,lightDirection,lightDistance);
    let segmentLength=max(0.0,slab.y-slab.x);
    var averageDensity=1.0;
    if(box.minimumFade.w>0 && segmentLength>0) {
        averageDensity=0;
        for(var i=0u;i<4u;i++) {
            let offset=slab.x+segmentLength*(f32(i)+0.5)*0.25;
            averageDensity+=fogDensityScale(box,samplePoint+lightDirection*offset)*0.25;
        }
    }
    return box.colorDensity.w*segmentLength*averageDensity;
}
fn fogDirectOpticalDepth(f:Fog,samplePoint:vec3f,lightDirection:vec3f,lightDistance:f32) -> f32 {
    // Camera distance caps do not clip the path from this sample to the light.
    var opticalDepth=0.0;
    let count=fogVolumeCount(f);
    for(var i=0u;i<count;i++) {
        opticalDepth+=fogBoxOpticalDepth(fogBox(f,i),samplePoint,lightDirection,lightDistance);
    }
    return opticalDepth;
}
fn fogLightBasis(direction:vec3f) -> mat3x3f {
    let forward=normalize(direction);
    let reference=select(vec3f(0,1,0),vec3f(1,0,0),abs(forward.y)>0.99);
    let right=normalize(cross(reference,forward));
    return mat3x3f(right,cross(forward,right),forward);
}
// FOG_CACHE_SAMPLING
fn fogSpotTransmission(index:i32,f:Fog,samplePoint:vec3f,lightDirection:vec3f,lightDistance:f32) -> f32 {
    if(!fogLightCacheEnabled) {
        return exp(-fogDirectOpticalDepth(f,samplePoint,lightDirection,lightDistance));
    }
    for(var slot=0u;slot<u32(f.volumeLighting.z);slot++) {
        let cache=f.lightCache[slot];
        if(i32(cache.minimumIndex.w)!=index) {continue;}
        let light=frame.lights[u32(index)];
        let local=transpose(fogLightBasis(light.directionRange.xyz))*(samplePoint-light.positionType.xyz);
        let bounds=FogVolumeData(cache.minimumIndex,cache.maximum,vec4f(0));
        // Clip the complete light-to-receiver segment, including receivers
        // beyond the fog. A light inside the domain starts at t=0.
        let range=fogBoxRange(bounds,vec3f(0),local,1);
        if(range.x>=range.y) {return 1;}
        let endpoint=local*range.y;
        let coordinates=(endpoint-cache.minimumIndex.xyz)/(cache.maximum.xyz-cache.minimumIndex.xyz);
        return exp(-fogCacheOpticalDepth(coordinates,slot));
    }
    return exp(-fogDirectOpticalDepth(f,samplePoint,lightDirection,lightDistance));
}
fn fogVolumeLightContribution(index:i32,p:vec3f) -> vec3f {
    let light=frame.lights[u32(index)];
    var attenuation=1.0;
    if(light.positionType.w>1.5) {
        let delta=p-light.positionType.xyz;
        let lightDistance=length(delta);
        let distance=max(lightDistance,0.001);
        let rangeFade=clamp(1-distance/light.directionRange.w,0,1);
        let cosine=dot(light.directionRange.xyz,delta/distance);
        let cone=smoothstep(light.cone.y,max(light.cone.y+1e-5,light.cone.x),cosine);
        attenuation=rangeFade*rangeFade*cone;
        if(attenuation>0 && frame.fog.volumeOptions.z>0.5) {
            if(lightDistance>1e-6) {attenuation*=fogSpotTransmission(index,frame.fog,p,-delta/lightDistance,lightDistance);}
        }
    }
    if(attenuation>0) {
        return light.colorIntensity.rgb*(light.colorIntensity.w*attenuation*fogVolumeShadow(index,p)*0.07957747);
    }
    return vec3f(0);
}
fn fogVolumeLight(indices:vec4i,p:vec3f) -> vec3f {
    var result=max(frame.ambientCount.rgb,vec3f(0));
    for(var i=0u;i<4u;i++) {
        let index=indices[i];
        if(index<0) {break;}
        result+=fogVolumeLightContribution(index,p);
    }
    return result;
}
fn fogVolumeStep(f:Fog,origin:vec3f,direction:vec3f,offset:f32,ds:f32,start:f32,lights:vec4i,boxCount:u32,ranges:array<vec2f,4>) -> vec4f {
    let globalDepth=fogDepth(f,origin+direction*offset,origin+direction*(offset+ds),max(0.0,start-offset));
    var opticalDepth=globalDepth;
    var source=globalDepth*f.colorDensity.rgb;
    for(var i=0u;i<boxCount;i++) {
        var segmentStart=offset;
        var segmentLength=ds;
        if(boxCount>1u) {
            // Clip each box separately so a step cannot skip a narrow box.
            segmentStart=max(segmentStart,ranges[i].x);
            segmentLength=min(offset+ds,ranges[i].y)-segmentStart;
            if(segmentLength<=0) {continue;}
        }
        let box=fogBox(f,i);
        let p=origin+direction*(segmentStart+segmentLength*0.5);
        let localDepth=box.colorDensity.w*segmentLength*fogDensityScale(box,p);
        if(localDepth<=0) {continue;}
        opticalDepth+=localDepth;
        source+=localDepth*box.colorDensity.rgb*fogVolumeLight(lights,p);
    }
    let transmission=exp(-opticalDepth);
    // Mix sources by their share of the extinction in this step.
    return vec4f((1-transmission)*source/max(opticalDepth,1e-9),transmission);
}
fn fogVolumeSegment(origin:vec3f,endpoint:vec3f,traveled:f32) -> vec4f {
    let f=frame.fog;
    let delta=endpoint-origin;
    let distance=length(delta);
    let start=max(0.0,f.height.x-traveled);
    let direction=delta/max(distance,1e-6);
    let boxCount=fogVolumeCount(f);
    var ranges:array<vec2f,4>;
    var enter=distance;
    var leave=0.0;
    for(var i=0u;i<boxCount;i++) {
        let box=fogBox(f,i);
        let range=fogBoxRange(box,origin,direction,min(distance,box.maximumDistance.w-traveled));
        ranges[i]=range;
        if(range.x<range.y) {
            enter=min(enter,range.x);
            leave=max(leave,range.y);
        }
    }
    if(enter>=leave) {
        let t=exp(-fogDepth(f,origin,endpoint,start));
        return vec4f(f.colorDensity.rgb*(1-t),t);
    }
    var t=exp(-fogDepth(f,origin,origin+direction*enter,start));
    var scatter=f.colorDensity.rgb*(1-t);
    let steps=u32(f.volumeMinSteps.w);
    let ds=(leave-enter)/f32(steps);
    let lights=fogVolumeLightIndices();
    for(var i=0u;i<steps && t>0.0001;i++) {
        let step=fogVolumeStep(f,origin,direction,enter+f32(i)*ds,ds,start,lights,boxCount,ranges);
        scatter+=t*step.rgb;
        t*=step.a;
    }
    let tailT=exp(-fogDepth(f,origin+direction*leave,endpoint,max(0.0,start-leave)));
    return vec4f(scatter+t*(1-tailT)*f.colorDensity.rgb,t*tailT);
}

