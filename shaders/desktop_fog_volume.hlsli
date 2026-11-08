bool fogVolumeLightEligible(int index) {
    return lights[index].colorIntensity.w>0.0 && (lights[index].positionType.w<0.5 || lights[index].positionType.w>1.5);
}
int4 fogVolumeLightIndices() {
    // Prefer mapped lights, counting a light with multiple cascades only once.
    int budget=clamp(int(frameFog.volumeLighting.y),1,4);
    int4 indices=-1;
    int count=0;
    [loop] for(int j=0;j<4;j++) {
        int index=int(shadows[j].selection.x);
        if(shadows[j].options.x<0.0 || index<0 || index>=int(ambientCount.w)) continue;
        if(fogVolumeLightEligible(index) && !any(indices==index)) {
            indices[count]=index;
            count++;
            if(count==budget) return indices;
        }
    }
    [loop] for(int j=0;j<int(ambientCount.w);j++) {
        if(fogVolumeLightEligible(j) && !any(indices==j)) {
            indices[count]=j;
            count++;
            if(count==budget) return indices;
        }
    }
    return indices;
}
// A negative result marks samples outside the map projection.
float fogShadowMap(Shadow shadow,float3 p,bool filtered) {
    float4 clip=mul(shadow.matrix,float4(p,1));
    if(clip.w<=0.0) return -1.0;
    float3 ndc=clip.xyz/clip.w;
    float2 uv=ndc.xy*float2(.5,-.5)+.5;
    if(any(uv<0) || any(uv>1) || ndc.z<0.0 || ndc.z>1.0) return -1.0;
    float layer=shadow.options.x,reference=ndc.z-shadow.options.y;
    float center=shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv,layer),reference);
    if(!filtered) return center;
    float2 x=float2(shadow.options.w,0),y=x.yx;
    return (2.0*center
        +shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv+x,layer),reference)
        +shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv-x,layer),reference)
        +shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv+y,layer),reference)
        +shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv-y,layer),reference))/6.0;
}
float fogVolumeShadow(int index,float3 p) {
    float depth=dot(p-frameFog.eye.xyz,frameFog.forward.xyz);
    bool filtered=(int(frameFog.volumeOptions.w)&2)!=0;
    float primary=1.0,nextWeight=0.0;
    for(int j=0;j<4;j++) {
        const Shadow shadow=shadows[j];
        if(shadow.options.x<0) continue;
        const float4 selection=shadow.selection;
        if(int(selection.x)!=index || (selection.w>0.5 && depth>selection.z)) continue;
        float value=fogShadowMap(shadow,p,filtered);
        if(nextWeight>0.0) return value>=0.0?lerp(primary,value,nextWeight):primary;
        if(value<0.0) continue;
        if(filtered && selection.w>0.5 && j+1<4) {
            float start=lerp(selection.y,selection.z,0.9);
            const Shadow next=shadows[j+1];
            if(depth>start && next.options.x>=0 && next.selection.w>0.5 && int(next.selection.x)==index && depth<=next.selection.z) {
                primary=value;
                nextWeight=smoothstep(start,selection.z,depth);
                continue;
            }
        }
        return value;
    }
    return primary;
}
int fogVolumeCount(Fog f) {
    return clamp(int(f.volumeLighting.x),1,4);
}
FogVolumeData fogBox(Fog f,int index) {
    if(index==0) {
        FogVolumeData firstBox;
        firstBox.minimumFade=float4(f.volumeMinSteps.xyz,f.volumeOptions.x);
        firstBox.maximumDistance=f.volumeMaxDistance;
        firstBox.colorDensity=f.volumeColorDensity;
        return firstBox;
    }
    return f.additionalVolumes[index-1];
}
float2 fogBoxRange(FogVolumeData box,float3 origin,float3 direction,float limit) {
    float3 lo=box.minimumFade.xyz,hi=box.maximumDistance.xyz;
    float enter=0.0,leave=limit;
    for(int axis=0;axis<3;axis++) {
        if(abs(direction[axis])<1e-8) {
            if(origin[axis]<lo[axis] || origin[axis]>hi[axis]) leave=0.0;
        } else {
            float a=(lo[axis]-origin[axis])/direction[axis],b=(hi[axis]-origin[axis])/direction[axis];
            enter=max(enter,min(a,b));
            leave=min(leave,max(a,b));
        }
    }
    return float2(enter,leave);
}
float2 fogRayRange(Fog f,float3 origin,float3 direction,float limit) {
    return fogBoxRange(fogBox(f,0),origin,direction,limit);
}
float fogDensityScale(FogVolumeData box,float3 p) {
    float3 edge=min(p-box.minimumFade.xyz,box.maximumDistance.xyz-p);
    return clamp(min(edge.x,min(edge.y,edge.z))/max(box.minimumFade.w,1e-6),0.0,1.0);
}
float fogBoxOpticalDepth(FogVolumeData box,float3 samplePoint,float3 lightDirection,float lightDistance) {
    float2 slab=fogBoxRange(box,samplePoint,lightDirection,lightDistance);
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
float fogDirectOpticalDepth(Fog f,float3 samplePoint,float3 lightDirection,float lightDistance) {
    // Camera distance caps do not clip the path from this sample to the light.
    float opticalDepth=0.0;
    int count=fogVolumeCount(f);
    [loop] for(int i=0;i<count;i++) {
        opticalDepth+=fogBoxOpticalDepth(fogBox(f,i),samplePoint,lightDirection,lightDistance);
    }
    return opticalDepth;
}
Texture2D<float4> fogLightCache : register(t26);
void fogLightBasis(float3 direction,out float3 right,out float3 up,out float3 forward) {
    forward=normalize(direction);
    const float3 referenceUp=abs(forward.y)>.99?float3(1,0,0):float3(0,1,0);
    right=normalize(cross(referenceUp,forward));
    up=cross(forward,right);
}
float fogLightCacheSlice(float2 coordinate,float slice,int slot) {
    const float2 tile=float2(fmod(slice,8.0),floor(slice/8.0))*64.0;
    return fogLightCache.SampleLevel(clampClamp,(tile+coordinate+.5)/512.0,0)[slot];
}
float fogSpotTransmission(int index,Fog f,float3 samplePoint,float3 lightDirection,float lightDistance) {
    [loop] for(int slot=0;slot<int(f.volumeLighting.z);++slot) {
        const FogLightCacheData metadata=f.lightCache[slot];
        if(int(metadata.minimumIndex.w)!=index) continue;
        float3 right,up,forward;
        fogLightBasis(lights[index].directionRange.xyz,right,up,forward);
        const float3 relative=samplePoint-lights[index].positionType.xyz;
        const float3 localPoint=float3(dot(relative,right),dot(relative,up),dot(relative,forward));
        FogVolumeData bounds;
        bounds.minimumFade=metadata.minimumIndex;
        bounds.maximumDistance=metadata.maximum;
        bounds.colorDensity=0;
        const float2 interval=fogBoxRange(bounds,float3(0,0,0),localPoint,1.0);
        if(interval.x>=interval.y) return 1.0;
        // Beyond the bounds, extinction stays at the exit along this same ray.
        const float3 clipped=localPoint*min(interval.y,1.0);
        const float3 coordinate=saturate((clipped-metadata.minimumIndex.xyz)/(metadata.maximum.xyz-metadata.minimumIndex.xyz))*63.0;
        const float slice=floor(coordinate.z);
        const float opticalDepth=lerp(fogLightCacheSlice(coordinate.xy,slice,slot),
            fogLightCacheSlice(coordinate.xy,min(slice+1.0,63.0),slot),frac(coordinate.z));
        return exp(-opticalDepth);
    }
    return exp(-fogDirectOpticalDepth(f,samplePoint,lightDirection,lightDistance));
}
float4 fogLightCachePS(float4 pixel:SV_Position) : SV_Target {
    const float2 texel=floor(pixel.xy);
    const float slice=floor(texel.y/64.0)*8.0+floor(texel.x/64.0);
    const float3 node=float3(fmod(texel.x,64.0),fmod(texel.y,64.0),slice)/63.0;
    float4 opticalDepth=0;
    [loop] for(int slot=0;slot<int(frameFog.volumeLighting.z);++slot) {
        const FogLightCacheData metadata=frameFog.lightCache[slot];
        const int index=int(metadata.minimumIndex.w);
        float3 right,up,forward;
        fogLightBasis(lights[index].directionRange.xyz,right,up,forward);
        const float3 localPoint=lerp(metadata.minimumIndex.xyz,metadata.maximum.xyz,node);
        const float3 relative=right*localPoint.x+up*localPoint.y+forward*localPoint.z;
        const float distance=length(relative);
        if(distance>1e-6) opticalDepth[slot]=min(80.0,fogDirectOpticalDepth(frameFog,
            lights[index].positionType.xyz+relative,-relative/distance,distance));
    }
    return opticalDepth;
}
float3 fogVolumeLightContribution(int index,float3 p) {
    float attenuation=1.0;
    if(lights[index].positionType.w>1.5) {
        float3 delta=p-lights[index].positionType.xyz;
        float lightDistance=length(delta);
        float distance=max(lightDistance,0.001);
        float rangeFade=clamp(1.0-distance/lights[index].directionRange.w,0.0,1.0);
        float cosine=dot(lights[index].directionRange.xyz,delta/distance);
        float2 cone=lights[index].cone.xy;
        attenuation=rangeFade*rangeFade*smoothstep(cone.y,max(cone.y+1e-5,cone.x),cosine);
        if(attenuation>0.0 && frameFog.volumeOptions.z>0.5) {
            if(lightDistance>1e-6) attenuation*=fogSpotTransmission(index,frameFog,p,-delta/lightDistance,lightDistance);
        }
    }
    if(attenuation>0.0) return lights[index].colorIntensity.rgb*(lights[index].colorIntensity.w*attenuation*fogVolumeShadow(index,p)*0.07957747);
    return float3(0,0,0);
}
float3 fogVolumeLight(int4 indices,float3 p) {
    float3 result=max(ambientCount.rgb,0);
    [loop] for(int i=0;i<4;i++) {
        int index=indices[i];
        if(index<0) break;
        result+=fogVolumeLightContribution(index,p);
    }
    return result;
}
float4 fogVolumeStep(Fog f,float3 origin,float3 direction,float offset,float ds,float start,int4 lights,int boxCount,float2 ranges[4]) {
    float globalDepth=fogDepth(f,origin+direction*offset,origin+direction*(offset+ds),max(0.0,start-offset));
    float opticalDepth=globalDepth;
    float3 source=globalDepth*f.colorDensity.rgb;
    [loop] for(int i=0;i<boxCount;i++) {
        float segmentStart=offset,segmentLength=ds;
        if(boxCount>1) {
            // Clip each box separately so a step cannot skip a narrow box.
            segmentStart=max(segmentStart,ranges[i].x);
            segmentLength=min(offset+ds,ranges[i].y)-segmentStart;
            if(segmentLength<=0.0) continue;
        }
        FogVolumeData box=fogBox(f,i);
        float3 p=origin+direction*(segmentStart+segmentLength*0.5);
        float localDepth=box.colorDensity.w*segmentLength*fogDensityScale(box,p);
        if(localDepth<=0.0) continue;
        opticalDepth+=localDepth;
        source+=localDepth*box.colorDensity.rgb*fogVolumeLight(lights,p);
    }
    float transmission=exp(-opticalDepth);
    // Mix sources by their share of the extinction in this step.
    return float4((1.0-transmission)*source/max(opticalDepth,1e-9),transmission);
}
float4 fogVolumeSegment(float3 origin,float3 endpoint,float traveled) {
    Fog f=frameFog;
    float3 delta=endpoint-origin;
    float distance=length(delta),start=max(0.0,f.height.x-traveled);
    float3 direction=delta/max(distance,1e-6);
    int boxCount=fogVolumeCount(f);
    float2 ranges[4]={float2(0,0),float2(0,0),float2(0,0),float2(0,0)};
    float enter=distance,leave=0.0;
    [loop] for(int i=0;i<boxCount;i++) {
        FogVolumeData box=fogBox(f,i);
        float2 range=fogBoxRange(box,origin,direction,min(distance,box.maximumDistance.w-traveled));
        ranges[i]=range;
        if(range.x<range.y) {
            enter=min(enter,range.x);
            leave=max(leave,range.y);
        }
    }
    if(enter>=leave) {
        float t=exp(-fogDepth(f,origin,endpoint,start));
        return float4(f.colorDensity.rgb*(1.0-t),t);
    }
    float t=exp(-fogDepth(f,origin,origin+direction*enter,start));
    float3 scatter=f.colorDensity.rgb*(1.0-t);
    int steps=int(f.volumeMinSteps.w);
    int4 indices=fogVolumeLightIndices();
    float ds=(leave-enter)/float(steps);
    [loop] for(int i=0;i<steps && t>0.0001;i++) {
        float4 stepFog=fogVolumeStep(f,origin,direction,enter+float(i)*ds,ds,start,indices,boxCount,ranges);
        scatter+=t*stepFog.rgb;
        t*=stepFog.a;
    }
    float tailT=exp(-fogDepth(f,origin+direction*leave,endpoint,max(0.0,start-leave)));
    return float4(scatter+t*(1.0-tailT)*f.colorDensity.rgb,t*tailT);
}

