uniform vec3 ambient;
uniform vec4 lightPosition[8],lightDirection[8],lightColor[8],lightCone[8];
uniform int lightCount,shadowCount,shadowTaps;
uniform highp sampler2DArrayShadow shadowMaps;
uniform mat4 shadowMatrix[4];
uniform vec4 shadowOptions[4],shadowSelection[4];
bool fogVolumeLightEligible(int index) {
    return lightColor[index].w>0.0 && (lightPosition[index].w<0.5 || lightPosition[index].w>1.5);
}
ivec4 fogVolumeLightIndices() {
    // Prefer mapped lights, counting a light with multiple cascades only once.
    int budget=clamp(int(fog.volumeLighting.y),1,4);
    ivec4 indices=ivec4(-1);
    int count=0;
    for(int j=0;j<shadowCount;j++) {
        int index=int(shadowSelection[j].x);
        if(shadowOptions[j].x<0.0 || index<0 || index>=lightCount) continue;
        if(fogVolumeLightEligible(index) && !any(equal(indices,ivec4(index)))) {
            indices[count]=index;
            count++;
            if(count==budget) return indices;
        }
    }
    for(int j=0;j<lightCount;j++) {
        if(fogVolumeLightEligible(j) && !any(equal(indices,ivec4(j)))) {
            indices[count]=j;
            count++;
            if(count==budget) return indices;
        }
    }
    return indices;
}
// A negative result marks samples outside the map projection.
float fogShadowMap(int j,vec3 p,bool filtered) {
    vec4 clip=shadowMatrix[j]*vec4(p,1);
    if(clip.w<=0.0) return -1.0;
    vec3 ndc=clip.xyz/clip.w;
    vec2 uv=ndc.xy*0.5+0.5;
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))) || ndc.z<0.0 || ndc.z>1.0) return -1.0;
    vec2 layerReference=vec2(float(j),ndc.z-shadowOptions[j].y);
    float texel=shadowOptions[j].w;
    float visibility=0.0;
    int taps=filtered ? 5 : 1;
    for(int tap=0;tap<taps;++tap) {
        vec2 axis=tap<3 ? vec2(texel,0) : vec2(0,texel);
        vec2 offset=tap==0 ? vec2(0) : ((tap&1)!=0 ? axis : -axis);
        float weight=filtered && tap==0 ? 2.0 : 1.0;
        visibility+=weight*texture(shadowMaps,vec4(uv+offset,layerReference));
    }
    return visibility/(filtered ? 6.0 : 1.0);
}
float fogVolumeShadow(int index,vec3 p) {
    float depth=dot(p-fog.eye.xyz,fog.forward.xyz);
    bool filtered=(int(fog.volumeOptions.w)&2)!=0;
    float primary=1.0,nextWeight=0.0;
    for(int j=0;j<shadowCount;j++) {
        vec4 selection=shadowSelection[j];
        if(int(selection.x)!=index || (selection.w>0.5 && depth>selection.z)) continue;
        float value=fogShadowMap(j,p,filtered);
        if(nextWeight>0.0) return value>=0.0?mix(primary,value,nextWeight):primary;
        if(value<0.0) continue;
        if(filtered && selection.w>0.5 && j+1<shadowCount) {
            float start=mix(selection.y,selection.z,0.9);
            vec4 next=shadowSelection[j+1];
            if(depth>start && next.w>0.5 && int(next.x)==index && depth<=next.z) {
                primary=value;
                nextWeight=smoothstep(start,selection.z,depth);
                continue;
            }
        }
        return value;
    }
    return primary;
}
int fogVolumeCount() {
    return clamp(int(fog.volumeLighting.x),1,4);
}
FogVolumeData fogBox(int index) {
    if(index==0) {
        return FogVolumeData(vec4(fog.volumeMinSteps.xyz,fog.volumeOptions.x),fog.volumeMaxDistance,fog.volumeColorDensity);
    }
    return fog.additionalVolumes[index-1];
}
vec2 fogBoxRange(FogVolumeData box,vec3 origin,vec3 direction,float limit) {
    vec3 lo=box.minimumFade.xyz,hi=box.maximumDistance.xyz;
    bvec3 parallel=lessThan(abs(direction),vec3(1e-8));
    vec3 outside=max(lo-origin,origin-hi);
    if(any(greaterThan(mix(vec3(0),outside,parallel),vec3(0)))) return vec2(1,0);
    vec3 reciprocal=1.0/mix(direction,vec3(1),parallel);
    vec3 a=(lo-origin)*reciprocal,b=(hi-origin)*reciprocal;
    vec3 entry=mix(min(a,b),vec3(-1e20),parallel),exit=mix(max(a,b),vec3(1e20),parallel);
    return vec2(max(0.0,max(entry.x,max(entry.y,entry.z))),min(limit,min(exit.x,min(exit.y,exit.z))));
}
vec2 fogRayRange(vec3 origin,vec3 direction,float limit) {
    return fogBoxRange(fogBox(0),origin,direction,limit);
}
float fogDensityScale(FogVolumeData box,vec3 p) {
    vec3 edge=min(p-box.minimumFade.xyz,box.maximumDistance.xyz-p);
    return clamp(min(edge.x,min(edge.y,edge.z))/max(box.minimumFade.w,1e-6),0.0,1.0);
}
float fogBoxOpticalDepth(FogVolumeData box,vec3 samplePoint,vec3 lightDirection,float lightDistance) {
    vec2 slab=fogBoxRange(box,samplePoint,lightDirection,lightDistance);
    float segmentLength=max(0.0,slab.y-slab.x),averageDensity=1.0;
    if(box.minimumFade.w>0.0 && segmentLength>0.0) {
        averageDensity=0.0;
        for(int i=0;i<4;i++) {
            float offset=slab.x+segmentLength*(float(i)+0.5)*0.25;
            averageDensity+=fogDensityScale(box,samplePoint+lightDirection*offset)*0.25;
        }
    }
    return box.colorDensity.w*segmentLength*averageDensity;
}
float fogLightOpticalDepth(vec3 samplePoint,vec3 lightDirection,float lightDistance) {
    // Camera distance caps do not clip the path from this sample to the light.
    float opticalDepth=0.0;
    int count=fogVolumeCount();
    for(int i=0;i<count;i++) {
        opticalDepth+=fogBoxOpticalDepth(fogBox(i),samplePoint,lightDirection,lightDistance);
    }
    return min(80.0,opticalDepth);
}
mat3 fogLightBasis(vec3 direction) {
    vec3 forward=normalize(direction);
    vec3 referenceUp=abs(forward.y)>0.99 ? vec3(1,0,0) : vec3(0,1,0);
    vec3 right=normalize(cross(referenceUp,forward));
    return mat3(right,cross(forward,right),forward);
}
#ifndef FOG_LIGHT_CACHE_BUILD
uniform highp sampler2D fogGrid;
uniform float fogCacheBaseY;
float fogLightCacheSlice(vec3 node,float slice,int slot) {
    vec2 tile=vec2(mod(slice,8.0),floor(slice/8.0))*64.0;
    vec2 uv=(tile+node.xy+vec2(0.5,fogCacheBaseY+0.5))/vec2(textureSize(fogGrid,0));
    return textureLod(fogGrid,uv,0.0)[slot];
}
float fogCachedOpticalDepth(int slot,int lightIndex,vec3 p) {
    FogLightCacheData cache=fog.lightCache[slot];
    vec3 endpoint=transpose(fogLightBasis(lightDirection[lightIndex].xyz))*(p-lightPosition[lightIndex].xyz);
    // Clip the light-to-receiver segment. Beyond the domain the exit stores the whole medium path.
    vec3 lo=cache.minimumIndex.xyz,hi=cache.maximum.xyz;
    bvec3 parallel=lessThan(abs(endpoint),vec3(1e-8));
    if(any(greaterThan(mix(vec3(0),max(lo,-hi),parallel),vec3(0)))) return 0.0;
    vec3 reciprocal=1.0/mix(endpoint,vec3(1),parallel);
    vec3 a=lo*reciprocal,b=hi*reciprocal;
    vec3 entry=mix(min(a,b),vec3(-1e20),parallel),exit=mix(max(a,b),vec3(1e20),parallel);
    float enter=max(0.0,max(entry.x,max(entry.y,entry.z))),leave=min(1.0,min(exit.x,min(exit.y,exit.z)));
    if(enter>leave) return 0.0;
    vec3 node=clamp((endpoint*leave-lo)/(hi-lo),vec3(0),vec3(1))*63.0;
    float slice=floor(node.z);
    return mix(fogLightCacheSlice(node,slice,slot),fogLightCacheSlice(node,min(63.0,slice+1.0),slot),fract(node.z));
}
#endif
float fogSpotTransmission(int lightIndex,vec3 samplePoint,vec3 lightDirection,float lightDistance) {
#ifndef FOG_LIGHT_CACHE_BUILD
    for(int slot=0;slot<int(fog.volumeLighting.z);++slot) {
        if(int(fog.lightCache[slot].minimumIndex.w)==lightIndex) return exp(-fogCachedOpticalDepth(slot,lightIndex,samplePoint));
    }
#endif
    return exp(-fogLightOpticalDepth(samplePoint,lightDirection,lightDistance));
}
vec3 fogVolumeLightContribution(int index,vec3 p) {
    float attenuation=1.0;
    if(lightPosition[index].w>1.5) {
        vec3 delta=p-lightPosition[index].xyz;
        float lightDistance=length(delta);
        float distance=max(lightDistance,0.001);
        float rangeFade=clamp(1.0-distance/lightDirection[index].w,0.0,1.0);
        float cosine=dot(lightDirection[index].xyz,delta/distance);
        vec2 cone=lightCone[index].xy;
        attenuation=rangeFade*rangeFade*smoothstep(cone.y,max(cone.y+1e-5,cone.x),cosine);
        if(attenuation>0.0 && fog.volumeOptions.z>0.5) {
            if(lightDistance>1e-6) attenuation*=fogSpotTransmission(index,p,-delta/lightDistance,lightDistance);
        }
    }
    if(attenuation>0.0) return lightColor[index].rgb*(lightColor[index].w*attenuation*fogVolumeShadow(index,p)*0.07957747);
    return vec3(0);
}
vec3 fogVolumeLight(ivec4 indices,vec3 p) {
    vec3 result=max(ambient,vec3(0));
    int budget=clamp(int(fog.volumeLighting.y),1,4);
    for(int i=0;i<budget;i++) {
        int index=indices[i];
        if(index<0) break;
        result+=fogVolumeLightContribution(index,p);
    }
    return result;
}
vec4 fogVolumeStep(vec3 origin,vec3 direction,float offset,float ds,float start,ivec4 lights,int boxCount,vec2 ranges[4]) {
    float globalDepth=fogDepth(fog,origin+direction*offset,origin+direction*(offset+ds),max(0.0,start-offset));
    float opticalDepth=globalDepth;
    vec3 source=globalDepth*fog.colorDensity.rgb;
    for(int i=0;i<boxCount;i++) {
        float segmentStart=offset,segmentLength=ds;
        if(boxCount>1) {
            // Clip each box separately so a step cannot skip a narrow box.
            segmentStart=max(segmentStart,ranges[i].x);
            segmentLength=min(offset+ds,ranges[i].y)-segmentStart;
            if(segmentLength<=0.0) continue;
        }
        FogVolumeData box=fogBox(i);
        vec3 p=origin+direction*(segmentStart+segmentLength*0.5);
        float localDepth=box.colorDensity.w*segmentLength*fogDensityScale(box,p);
        if(localDepth<=0.0) continue;
        opticalDepth+=localDepth;
        source+=localDepth*box.colorDensity.rgb*fogVolumeLight(lights,p);
    }
    float transmission=exp(-opticalDepth);
    // Mix sources by their share of the extinction in this step.
    return vec4((1.0-transmission)*source/max(opticalDepth,1e-9),transmission);
}
vec4 fogVolumeSegment(vec3 origin,vec3 endpoint,float traveled) {
    vec3 delta=endpoint-origin;
    float distance=length(delta),start=max(0.0,fog.height.x-traveled);
    vec3 direction=delta/max(distance,1e-6);
    int boxCount=fogVolumeCount();
    vec2 ranges[4]=vec2[4](vec2(0),vec2(0),vec2(0),vec2(0));
    float enter=distance,leave=0.0;
    for(int i=0;i<boxCount;i++) {
        FogVolumeData box=fogBox(i);
        vec2 range=fogBoxRange(box,origin,direction,min(distance,box.maximumDistance.w-traveled));
        ranges[i]=range;
        if(range.x<range.y) {
            enter=min(enter,range.x);
            leave=max(leave,range.y);
        }
    }
    if(enter>=leave) {
        float t=exp(-fogDepth(fog,origin,endpoint,start));
        return vec4(fog.colorDensity.rgb*(1.0-t),t);
    }
    float t=exp(-fogDepth(fog,origin,origin+direction*enter,start));
    vec3 scatter=fog.colorDensity.rgb*(1.0-t);
    int steps=int(fog.volumeMinSteps.w);
    ivec4 indices=fogVolumeLightIndices();
    float ds=(leave-enter)/float(steps);
    for(int i=0;i<steps && t>0.0001;i++) {
        vec4 stepFog=fogVolumeStep(origin,direction,enter+float(i)*ds,ds,start,indices,boxCount,ranges);
        scatter+=t*stepFog.rgb;
        t*=stepFog.a;
    }
    float tailT=exp(-fogDepth(fog,origin+direction*leave,endpoint,max(0.0,start-leave)));
    return vec4(scatter+t*(1.0-tailT)*fog.colorDensity.rgb,t*tailT);
}

