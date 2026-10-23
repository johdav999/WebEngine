// Existing water presentation math; no simulation or clock ownership.
void waterVertex(inout float3 p,inout float3 n,inout float3 t,inout float3 b){
    float3 dx=float3(1,0,0),dz=float3(0,0,1),position=p;
    [unroll]for(uint i=0;i<3;i++){
        const float angle=float(i)*1.17,c=cos(angle),s=sin(angle);
        const float2 d=float2(c*waterDetail.x-s*waterDetail.y,s*waterDetail.x+c*waterDetail.y);
        const float weight=i==0?.55:i==1?.3:.15,scale=i==0?1:i==1?.57:.31;
        const float k=6.2831853/(wave.y*scale),a=wave.x*weight,q=min(wave.w,.8/max(3*k*a,.0001));
        const float phase=k*dot(d,p.xz)-sqrt(9.81*k)*waterClock.x*wave.z,sn=sin(phase),cs=cos(phase);
        position+=float3(q*a*d.x*cs,a*sn,q*a*d.y*cs);
        dx+=float3(-q*a*k*d.x*d.x*sn,a*k*d.x*cs,-q*a*k*d.x*d.y*sn);
        dz+=float3(-q*a*k*d.x*d.y*sn,a*k*d.y*cs,-q*a*k*d.y*d.y*sn);
    }
    p=position;n=safeNormal(cross(dz,dx),float3(0,1,0));t=dx;b=-dz;
}
float3 linearDisplay(float3 c){if(outputOptions.x>.5)return c;return float3(c.x>.04045?pow((c.x+.055)/1.055,2.4):c.x/12.92,c.y>.04045?pow((c.y+.055)/1.055,2.4):c.y/12.92,c.z>.04045?pow((c.z+.055)/1.055,2.4):c.z/12.92);}
float3 surfaceFog(float3 color,MeshOutput input){
    if(maps.z<.5)return color;
    if(reflectionOptions.y>.5&&frameFog.colorDensity.w>0){
        const float3 n=reflectionPlane.xyz,p=input.world,reflectedForward=reflect(frameFog.forward.xyz,n);
        const float3 virtualEye=frameFog.eye.w>.5?p-reflectedForward*(dot(p-cameraEnvironment.xyz,reflectedForward)-frameFog.forward.w):cameraEnvironment.xyz;
        const float3 delta=p-virtualEye;const float denom=dot(n,delta),amount=saturate(-dot(reflectionPlane,float4(virtualEye,1))/(abs(denom)>1e-6?denom:1e-6));
        const float3 hit=virtualEye+delta*amount,eye=virtualEye-2*dot(reflectionPlane,float4(virtualEye,1))*n;
        const float first=length(hit-eye),depth=fogDepth(frameFog,eye,hit,frameFog.height.x)+fogDepth(frameFog,hit,p,max(0,frameFog.height.x-first));
        return lerp(frameFog.colorDensity.rgb,color,exp(-depth));
    }
    return fogApply(frameFog,color,input.world,maps.z>1.5);
}
float3 scenePoint(float2 pixel,float depth){const float2 ndc=(pixel-waterViewport.xy)/waterViewport.zw*float2(2,-2)+float2(-1,1);const float4 position=mul(inverseViewProjection,float4(ndc,depth,1));return position.xyz/position.w;}
float3 waterComposite(MeshOutput input,float3 normal,float3 body,float3 display,float time,float nv){
    if(maps.z<.5||waterExtent.z<.5)return display;
    const float2 lo=waterViewport.xy+.5,hi=waterViewport.xy+waterViewport.zw-.5,basePixel=clamp(input.position.xy,lo,hi);
    const float baseDepth=sceneWaterDepth.Load(int3(int2(basePixel),0));if(baseDepth>=.999999||baseDepth<=input.position.z+.000001)return display;
    const float3 background=scenePoint(basePixel,baseDepth);const float viewThickness=max(dot(background-input.world,viewForward.xyz),0);
    const float2 projected=mul(viewProjection,float4(normal,0)).xy*float2(1,-1);
    float2 pixel=clamp(basePixel+projected*waterViewport.zw*transmission.y*smoothstep(0,.5,viewThickness),lo,hi);
    const float sampleDepth=sceneWaterDepth.Load(int3(int2(pixel),0));float3 samplePoint=scenePoint(pixel,min(sampleDepth,.999999));
    if(sampleDepth>=.999999||dot(samplePoint-input.world,viewForward.xyz)<=.001||dot(samplePoint-input.world,input.planeNormal)>.01){pixel=basePixel;samplePoint=background;}
    const float3 ray=safeNormal(scenePoint(basePixel,.99)-scenePoint(basePixel,0),viewForward.xyz);
    const float thickness=clamp(dot(samplePoint-input.world,viewForward.xyz)/max(dot(ray,viewForward.xyz),.1),0,100);
    const float3 transmitted=linearDisplay(sceneWaterColor.SampleLevel(clampClamp,pixel/waterExtent.xy,0).rgb),attenuation=exp(-absorptionFoam.rgb*thickness);
    const float fresnel=.02+.98*pow(1-saturate(nv),5);
    float3 result=lerp(display,transmitted*attenuation+surfaceFog(saturate(body),input)*(1-attenuation),transmission.x*(1-fresnel*waterHorizon.w));
    const float gap=max(dot(input.world-background,input.planeNormal),0),pattern=.65+.35*sin(input.uv.x*absorptionFoam.w*6.2831853+time*.8)*sin(input.uv.y*absorptionFoam.w*5.1-time*.6);
    const float foam=transmission.z*(1-smoothstep(0,transmission.w,gap))*pattern;return lerp(result,surfaceFog(foamColor.rgb,input),foam);
}
float interactionHeight(float3 p,float3 n){
    float height=0;
    for(uint i=0;i<uint(interactionOptions.x);i++){
        const float4 origin=interactionData[i*4],heading=interactionData[i*4+1],shape=interactionData[i*4+2];const float life=interactionData[i*4+3].x;
        float3 delta=p-origin.xyz;const float gap=dot(delta,n);delta-=n*gap;if(abs(gap)>shape.y)continue;
        float phase=length(delta)-origin.w*shape.w,envelope=exp(-pow(phase/shape.y,2));
        if(heading.w>0){const float3 projected=heading.xyz-n*dot(heading.xyz,n);if(dot(projected,projected)<1e-6)continue;const float3 forward=normalize(projected);
            const float along=-dot(delta,forward),side=abs(dot(delta,cross(n,forward)));if(along<0||along>heading.w)continue;
            phase=side-along*.32;envelope=exp(-pow(phase/shape.y,2))*smoothstep(0,.3,along)*(1-smoothstep(heading.w*.65,heading.w,along));phase-=origin.w*shape.w;
        }
        height+=shape.x*sin(6.2831853*phase/shape.z)*envelope*pow(1-origin.w/life,2);
    }return height;
}
float4 interactionSurface(float3 p,float3 n){const float3 axis=abs(n.y)>.9?float3(1,0,0):float3(0,1,0),u=normalize(cross(n,axis)),v=cross(n,u);const float h=interactionHeight(p,n);
    return float4(u*(interactionHeight(p+u*.025,n)-h)/.025+v*(interactionHeight(p+v*.025,n)-h)/.025,h);}
