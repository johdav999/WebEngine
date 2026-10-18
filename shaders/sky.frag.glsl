#version 300 es
precision highp float;
// FOG_FUNCTIONS
in vec3 fogOffset;
in vec3 direction;
uniform vec4 skyHorizon,skyZenith,skyNadir,skySun,skyAir,skyClouds,skyCloudWind;
uniform sampler2D panorama;
out vec4 outputColor;
float boundary(vec3 p,vec3 d,float r) {float b=dot(p,d);return -b+sqrt(max(0.0,b*b-dot(p,p)+r*r));}
vec2 density(vec3 p) {float h=max(length(p)-6360.0,0.0);return exp(-vec2(h/8.0,h/1.2));}
vec3 atmosphere(vec3 d) {
    float daylight=smoothstep(-0.08,0.2,skySun.y);vec3 night=vec3(0.001,0.002,0.006);
    vec3 ground=skyNadir.xyz*(0.025+daylight*0.5);
    if(d.y< -0.08) return ground;
    vec3 ray=normalize(vec3(d.x,max(d.y,0.0),d.z));
    vec3 origin=vec3(0,6360.0+skyAir.w,0);
    vec3 betaR=vec3(0.005802,0.013558,0.033100)*skyAir.x,betaM=vec3(0.003996)*skyAir.y;
    float total=boundary(origin,ray,6460.0);vec2 optical=vec2(0);vec3 sumR=vec3(0),sumM=vec3(0);
    for(int i=0;i<8;i++) {
        float a=float(i)/8.0,b=float(i+1)/8.0,dt=(b*b-a*a)*total;
        vec3 p=origin+ray*((a*a+b*b)*0.5*total);vec2 rho=density(p);optical+=rho*dt*0.5;
        float q=dot(p,skySun.xyz),discriminant=q*q-dot(p,p)+6360.0*6360.0;
        bool blocked=discriminant>0.0 && -q-sqrt(max(discriminant,0.0))>0.0;
        if(!blocked) {
            float path=boundary(p,skySun.xyz,6460.0);vec2 lightDepth=vec2(0);
            for(int j=0;j<4;j++) {
                float x=float(j)/4.0,y=float(j+1)/4.0;
                lightDepth+=density(p+skySun.xyz*((x*x+y*y)*0.5*path))*((y*y-x*x)*path);
            }
            vec3 trans=exp(-betaR*(optical.x+lightDepth.x)-betaM*1.1*(optical.y+lightDepth.y));
            sumR+=trans*rho.x*dt;sumM+=trans*rho.y*dt;
        }
        optical+=rho*dt*0.5;
    }
    float mu=clamp(dot(d,skySun.xyz),-1.0,1.0),g=skyAir.z;
    float phaseR=3.0*(1.0+mu*mu)/(16.0*3.14159265);
    float phaseM=3.0*(1.0-g*g)*(1.0+mu*mu)/(8.0*3.14159265*(2.0+g*g)*pow(max(1.0+g*g-2.0*g*mu,0.001),1.5));
    vec3 trans=exp(-betaR*optical.x-betaM*1.1*optical.y);
    float disc=smoothstep(cos(0.009),cos(0.0045),mu)*smoothstep(-0.015,0.015,skySun.y);
    float moon=smoothstep(cos(0.012),cos(0.006),-mu)*(1.0-daylight);
    vec3 scattering=night+(sumR*betaR*phaseR+sumM*betaM*phaseM)*skySun.w+trans*disc*skySun.w+vec3(0.15,0.20,0.3)*moon;
    return mix(ground,scattering,smoothstep(-0.08,0.0,d.y));
}
float hash(vec2 p) {vec3 q=fract(vec3(p.x,p.y,p.x)*0.1031);q+=dot(q,q.yzx+vec3(33.33));return fract((q.x+q.y)*q.z);}
float noise(vec2 p) {
    vec2 a=floor(p),f=fract(p),u=f*f*(vec2(3)-2.0*f);
    return mix(mix(hash(a),hash(a+vec2(1,0)),u.x),mix(hash(a+vec2(0,1)),hash(a+vec2(1,1)),u.x),u.y);
}
float fbm(vec2 p) {
    vec2 q=p;float weight=0.5,value=0.0;
    for(int i=0;i<5;i++) {value+=weight*noise(q);q=q*2.03+vec2(17.1,9.2);weight*=0.5;}return value;
}
vec3 cloudLayer(vec3 d,vec3 background) {
    if(skyClouds.x<=0.0 || skyClouds.y<=0.0 || d.y<=0.02) return background;
    vec2 p=d.xz/max(d.y,0.03)*skyClouds.z+skyCloudWind.xy;
    float shape=smoothstep(1.0-skyClouds.x,1.0-skyClouds.x+0.2,fbm(p));
    float alpha=(1.0-exp(-shape*skyClouds.y*3.0))*smoothstep(0.02,0.15,d.y);
    float daylight=smoothstep(-0.08,0.2,skySun.y),shade=0.6+0.4*noise(p+skySun.xz*0.4);
    vec3 warm=vec3(1,0.45,0.22)*(1.0-smoothstep(0.0,0.3,skySun.y));
    vec3 color=vec3(0.015,0.025,0.05)+daylight*(vec3(1.8,1.9,2.1)+warm)*shade;
    return mix(background,color,alpha);
}
vec3 decodeSrgb(vec3 p) {return mix(p/12.92,pow((p+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),p));}
vec3 panoramaPixel(ivec2 p,ivec2 size) {
    p.x=(p.x%size.x+size.x)%size.x;p.y=clamp(p.y,0,size.y-1);return decodeSrgb(texelFetch(panorama,p,0).rgb);
}
vec3 samplePanorama(vec2 uv) {
    ivec2 size=textureSize(panorama,0);vec2 p=uv*vec2(size)-0.5;ivec2 cell=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(panoramaPixel(cell,size),panoramaPixel(cell+ivec2(1,0),size),f.x),
        mix(panoramaPixel(cell+ivec2(0,1),size),panoramaPixel(cell+ivec2(1,1),size),f.x),f.y);
}
void main() {
    vec3 d=normalize(direction);vec3 radiance=mix(skyHorizon.xyz,d.y>=0.0 ? skyZenith.xyz : skyNadir.xyz,abs(d.y));
    if(skyNadir.w>1.5) radiance=atmosphere(d);
    else if(skyNadir.w>0.5) {
        float c=cos(skyZenith.w),s=sin(skyZenith.w);vec3 rotated=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
        vec2 uv=vec2(atan(rotated.z,rotated.x)/6.28318530718+0.5,acos(clamp(rotated.y,-1.0,1.0))/3.14159265359);
        radiance=samplePanorama(uv);
    }
    vec3 linear=cloudLayer(d,radiance)*skyHorizon.w;
    outputColor=vec4(pow(fogApply(fog,linear/(vec3(1)+linear),fog.eye.xyz+fogOffset+d*(fog.height.w/max(dot(d,fog.forward.xyz),0.001)),false),vec3(1.0/2.2)),1);
}
