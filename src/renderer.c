#include "renderer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float p[3], n[3], c[3], paint;
} Vertex;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define GLSL "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
#else
#define GLSL "#version 330 core\n"
#endif
// Shared by the color and shadow passes so limbs and their shadows agree.
// TCA1: world origin/yaw, pose, body part, scale and width.
// TCF1: world origin/yaw, species, body part and the verified ground slopes.
// Wildlife follows a bounded circle; phase easing gives ground animals pauses.
#define CROWD_GLSL                                                                                \
    "layout(location=4) in vec4 aOrigin;layout(location=5) in vec4 aMotion;"                         \
    "uniform bool uCrowd,uWildlife;uniform float uCrowdTime,uWind;"                                           \
    "mat3 turnY(float a){float c=cos(a),s=sin(a);return mat3(c,0.,-s,0.,1.,0.,s,0.,c);}"             \
    "mat3 turnZ(float a){float c=cos(a),s=sin(a);return mat3(c,s,0.,-s,c,0.,0.,0.,1.);}"             \
    "void animateWildlife(inout vec3 p,inout vec3 n){"                                              \
    "float seed=fract(sin(dot(aOrigin.xz,vec2(12.9898,78.233)))*43758.5453);"                        \
    "float kind=aMotion.x,part=aMotion.y,t=uCrowdTime+seed*30.;p-=aOrigin.xyz;"                     \
    "bool bird=kind>3.5;float phase=t*(bird?.19:kind>2.5?.32:.24);"                                \
    "float a=bird?phase:phase-sin(phase),radius=bird?12.:.65;"                                     \
    "float gait=sin(t*5.),activity=1.-cos(phase);"                                                \
    "if(bird && part>.5){float side=part<1.5?-1.:1.;"                                             \
    "mat3 flap=turnZ(side*.65*sin(t*5.5));vec3 pivot=vec3(side*.12,0.,0.);"                        \
    "p=pivot+flap*(p-pivot);n=flap*n;}"                                                          \
    "else if(kind>2.5 && !bird){p.x+=.10*sin(t*4.-p.z*7.);"                                     \
    "n=normalize(vec3(n.x,n.y,n.z+.7*cos(t*4.-p.z*7.)*n.x));}"                                 \
    "else if(part>.5 && part<2.5){p.z+=.10*gait*(part<1.5?1.:-1.)*activity;"                     \
    "p.y+=.055*max(0.,gait*(part<1.5?1.:-1.))*activity;}"                                       \
    "if(kind>.5 && kind<1.5)p.y+=.32*pow(max(0.,sin(t*5.)),2.)*activity;"                        \
    "if(part>2.5 && kind<.5){float graze=1.45*(.5+.5*sin(t*.6))*(1.-activity*.5);"                 \
    "float c=cos(graze),s=sin(graze);mat3 nod=mat3(1.,0.,0.,0.,c,s,0.,-s,c);"                   \
    "vec3 pivot=vec3(0.,1.28,.5);p=pivot+nod*(p-pivot);n=nod*n;}"                                \
    "mat3 heading=turnY(-a);p=heading*p;n=heading*n;"                                            \
    "p.xz+=radius*vec2(cos(a),sin(a));"                                                         \
    "if(bird)p.y+=.6*sin(t*.7);else{p.y+=dot(p.xz,aMotion.zw);"                                  \
    "n=normalize(vec3(n.x-aMotion.z*n.y,n.y,n.z-aMotion.w*n.y));}"                               \
    "p+=aOrigin.xyz;}"                                                                          \
    "void animateCrowd(inout vec3 p,inout vec3 n){if(uWildlife){animateWildlife(p,n);return;}"     \
    "if(!uCrowd)return;"                               \
    "float seed=fract(sin(dot(aOrigin.xz,vec2(12.9898,78.233)))*43758.5453);"                        \
    "float t=uCrowdTime*(3.8+seed*2.4)+seed*6.283185;"                                              \
    "float pose=aMotion.x,part=aMotion.y,size=aMotion.z;"                                          \
    "mat3 local=turnY(-aOrigin.w),world=turnY(aOrigin.w);"                                         \
    "p=local*(p-aOrigin.xyz)/size;n=local*n;"                                                      \
    "if(part>2.5){float x=max(0.,p.x-.64),w=t*2.-x*7.;"                                           \
    "float amp=.18+uWind*.025;w+=uWind*.3;"                                                       \
    "p.z+=amp*x*sin(w);n=normalize(vec3(n.x-amp*(sin(w)-7.*x*cos(w))*n.z,n.y,n.z));}"              \
    "if(part>.5 && abs(pose-3.)>.1){float side=part<1.5?-1.:1.,a=0.;"                             \
    "vec3 pivot=vec3(side*.30*aMotion.w,1.45,0.);mat3 arm;"                                       \
    "if(abs(pose-2.)<.1){a=side*(-.045+.60*(.5+.5*sin(t*1.6)));arm=turnY(a);}"                    \
    "else{if(pose<.5)a=side*.28*sin(t+side*.9);"                                                  \
    "else if(pose<1.5)a=side>0.?.48*sin(t):.07*sin(t);"                                          \
    "else a=side>0.?.32*sin(t*.8):.06*sin(t);arm=turnZ(a);}"                                     \
    "p=pivot+arm*(p-pivot);n=arm*n;}"                                                             \
    "float sway=(abs(pose-3.)<.1?.035:.075)*sin(t*.65);"                                         \
    "mat3 body=turnZ(sway*clamp((p.y-.25)/.7,0.,1.));p=body*p;n=body*n;"                         \
    "if(pose<.5){float hop=max(0.,sin(t*.8));p.y+=.32*hop*hop;}"                                 \
    "p=aOrigin.xyz+world*p*size;n=world*n;}\n"
// TCV1 reuses the eight animation attributes: root/height, flexibility, frond, reserved.
// The height-squared bend anchors roots; every part of a tree shares its phase.
#define VEGETATION_GLSL \
    "uniform bool uVegetation;uniform vec3 uVegetationWind;uniform float uVegetationTime;" \
    "void animateVegetation(inout vec3 p,inout vec3 n){if(!uVegetation)return;" \
    "vec3 q=p-aOrigin.xyz;float h=aOrigin.w;" \
    "float seed=dot(aOrigin.xz,vec2(.73,1.21)),t=uVegetationTime;" \
    "if(aMotion.y>.5){vec2 leaf=q.xz-vec2(h*.1,0.);" \
    "float flutter=.055*length(uVegetationWind)*sin(t*3.4+seed)/h;" \
    "p.y+=dot(leaf,leaf)*flutter;n.xz-=2.*leaf*flutter*n.y;}" \
    "float y=max(0.,p.y-aOrigin.y);" \
    "float sway=.35+.60*sin(t*1.6+seed)+.18*sin(t*2.43+seed*.7);" \
    "vec2 bend=uVegetationWind.xz*(.045*sqrt(aMotion.x)*sway);" \
    "p.xz+=bend*y*y/h;n.y-=dot(bend*(2.*y/h),n.xz);n=normalize(n);}"
static const char *world_vs =
    GLSL "layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;layout(location=2) in "
         "vec3 aColor;layout(location=3) in float aPaint;\n"
         "uniform mat4 uModel,uVP,uLight;uniform vec3 uPaint;out float vGround;out vec3 vPos,vNormal,vColor;out vec4 "
         "vShadow;\n"
         CROWD_GLSL VEGETATION_GLSL
         "void main(){vec3 pos=aPos,normal=aNormal;animateCrowd(pos,normal);animateVegetation(pos,normal);vec4 "
         "p=uModel*vec4(pos,1.);vPos=p.xyz;vNormal=mat3(uModel)*normal;vColor=mix(aColor,uPaint,"
         "clamp(aPaint,0.,1.));vGround=max(0.,-aPaint);vShadow=uLight*p;gl_Position=uVP*p;}";
// One world-space cloud layer for terrain, props and water reflections. Project
// along the sun direction onto a plane 110 m above the track, including elevation.
// Integrated weather drift avoids jumps when wind changes; no extra render pass.
#define CLOUD_GLSL \
    "uniform vec2 uCloudOffset;uniform float uCloudAmount;" \
    "float cloudHash(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);" \
    "return fract((q.x+q.y)*q.z);}" \
    "float cloudNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);" \
    "return mix(mix(cloudHash(i),cloudHash(i+vec2(1,0)),f.x)," \
    "mix(cloudHash(i+vec2(0,1)),cloudHash(i+1.),f.x),f.y);}" \
    "float cloudCover(vec2 p){vec2 q=(p-uCloudOffset)*.022;" \
    "float d=cloudNoise(q)*.65+cloudNoise(q*2.03+19.)*.25+cloudNoise(q*4.07+41.)*.10;" \
    "return smoothstep(.36,.66,d);}" \
    "float cloudShade(vec3 p){return cloudCover(p.xz+vec2(-.6,.45)*(110.-p.y))*uCloudAmount;}"
static const char *world_fs = GLSL CLOUD_GLSL
    "in float vGround;in vec3 vPos,vNormal,vColor;in vec4 vShadow;out vec4 frag;uniform sampler2D uShadow;uniform "
    "vec3 uEye,uFog;uniform float uFogFar,uUnlit,uOvercast;uniform bool uBoost;uniform float uBoostTime;\n"
    "float terrainHash(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);"
    "return fract((q.x+q.y)*q.z);}"
    "float terrainNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);"
    "return mix(mix(terrainHash(i),terrainHash(i+vec2(1,0)),f.x),"
    "mix(terrainHash(i+vec2(0,1)),terrainHash(i+vec2(1,1)),f.x),f.y);}"
    "float shadow(vec3 n){vec3 "
    "p=vShadow.xyz/vShadow.w*.5+.5;if(p.x<0.||p.x>1.||p.y<0.||p.y>1.||p.z>1.)return 1.;float "
    "bias=max(.0005,.0015*(1.-dot(n,normalize(vec3(-.6,1.,.45)))));float s=0.;for(int "
    "x=-1;x<=1;x++)for(int y=-1;y<=1;y++){float "
    "d=texture(uShadow,p.xy+vec2(x,y)/2048.).r;s+=p.z-bias<=d?1.:0.;}return s/9.;}\n"
    "void main(){vec3 n=normalize(vNormal);if(!gl_FrontFacing)n=-n;vec3 "
    "light=normalize(vec3(-.6,1.,.45));float diff=max(0.,dot(n,light));float sh=shadow(n);float "
    "hemi=n.y*.5+.5;vec3 ambient=mix(vec3(.33,.38,.43),vec3(.71,.75,.74),hemi);vec3 "
    "base=vColor;if(uBoost){vec2 p=vColor.rg;"
    "float phase=(p.y+abs(p.x)*.7-uBoostTime*1.35)/2.;"
    "float stripe=abs(fract(phase)-.5);float aa=max(fwidth(phase),.008);"
    "float arrow=1.-smoothstep(.10-aa,.10+aa,stripe);"
    "float inset=1.-smoothstep(2.20,2.55,abs(p.x));"
    "float ends=smoothstep(0.,.3,p.y)*(1.-smoothstep(5.7,6.,p.y));"
    "base=mix(vec3(.19,.23,.22),vec3(.36,.56,.48),arrow*inset*ends);"
    "}if(vGround>.5){float patch=terrainNoise(vPos.xz*.13);"
    "float detail=(terrainNoise(vPos.xz*3.5)-.5)*(1.-smoothstep(.12,.65,length(fwidth(vPos.xz))));"
    "if(vGround<1.5){base*=.86+.22*patch+.10*detail;"
    "base=mix(base,base*vec3(1.13,.94,.74),smoothstep(.52,.8,patch)*.4);}"
    "else{float ripple=sin(vPos.x*.9+vPos.z*.43+patch*5.);"
    "base*=.93+.09*patch+.04*detail+.018*ripple;} }float "
    "cloud=cloudShade(vPos);float sun=(1.-.8*uOvercast)*(1.-.82*cloud);"
    "vec3 skyAmbient=ambient*mix(vec3(1.),vec3(.91,.95,1.02),cloud);"
    "vec3 col=base*(skyAmbient*(1.-.18*uOvercast)+vec3(.39,.34,.27)*diff*sh*sun);vec3 "
    "halfV=normalize(light+normalize(uEye-vPos));float "
    "spec=pow(max(0.,dot(n,halfV)),48.)*.12*sh*sun;col+=spec;col=mix(col,base,uUnlit);float "
    "fog=smoothstep(uFogFar*.45,uFogFar,length(uEye-vPos));col=mix(col,uFog,fog*.88);frag=vec4(col,"
    "1.);}";
static const char *depth_vs = GLSL "layout(location=0)in vec3 aPos;uniform mat4 uModel,uVP;"
    CROWD_GLSL VEGETATION_GLSL "void main(){vec3 pos=aPos,n=vec3(0.,1.,0.);animateCrowd(pos,n);animateVegetation(pos,n);"
               "gl_Position=uVP*uModel*vec4(pos,1.);}";
static const char *depth_fs = GLSL "void main(){}";
// Instanced camera-facing quads: one draw, no per-drop objects or external assets.
// Positions wrap in world space; moving the chase camera never drags the weather.
static const char *weather_vs = GLSL
    "uniform mat4 uVP;uniform vec3 uCenter,uRight,uUp,uDrift,uWind;"
    "uniform float uTime,uIntensity;uniform int uKind,uTrack;"
    "out vec2 vUV;out vec4 vTint;"
    "float hash(float n){return fract(sin(n*127.1+311.7)*43758.5453);}"
    "void main(){float id=float(gl_InstanceID),a=hash(id+1.),b=hash(id+93.),c=hash(id+217.);"
    "bool rain=uKind==1,wind=uKind==3;"
    "vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),"
    "vec2(-1,-1),vec2(1,1),vec2(-1,1));vUV=corners[gl_VertexID];"
    "float fall=rain?24.+a*8.:wind?.5:2.+a*1.5;"
    "vec3 p=vec3(a*96.,b*60.-uTime*fall,c*96.)+uDrift;"
    "if(!rain){p.x+=sin(uTime*1.3+c*30.)*.9;p.z+=cos(uTime*.8+a*30.)*.7;}"
    "p.xz=uCenter.xz+mod(p.xz-uCenter.xz+48.,96.)-48.;"
    "p.y=uCenter.y+mod(p.y-uCenter.y+12.,60.)-12.;"
    "vec3 local=p-uCenter;float edge=1.-smoothstep(34.,48.,max(abs(local.x),abs(local.z)));"
    "edge*=smoothstep(-12.,-7.,local.y)*(1.-smoothstep(40.,48.,local.y));"
    "float size=rain?.035:wind?.08+a*.08:.10+a*.12;"
    "vec3 axis=rain?vec3(-uWind.x,fall,-uWind.z)*.022:uUp*size;"
    "if(wind)axis=uRight*size*sin(uTime*3.+a*40.)+uUp*size;"
    "p+=uRight*vUV.x*size+axis*vUV.y;"
    "vec3 tint=rain?vec3(.68,.81,.94):vec3(.95,.98,1.);"
    "if(wind && uTrack==0)tint=mix(vec3(.64,.31,.08),vec3(.9,.66,.24),b);"
    "if(wind && uTrack==1)tint=vec3(.89,.80,.59);"
    "vTint=vec4(tint,uIntensity*edge*(rain?.48:wind?.6:.82));gl_Position=uVP*vec4(p,1.);}";
static const char *weather_fs = GLSL
    "in vec2 vUV;in vec4 vTint;out vec4 frag;uniform int uKind;"
    "void main(){float mask=uKind==1?1.-abs(vUV.x):1.-smoothstep(.35,1.,length(vUV));"
    "float alpha=vTint.a*mask;if(alpha<.01)discard;frag=vec4(vTint.rgb,alpha);}";
// Water is drawn over an immutable copy of the opaque scene. Sampling a texture
// attached to the current draw framebuffer would be undefined on GL and GLES.
static const char *water_vs = GLSL
    "layout(location=0)in vec3 aPos;layout(location=2)in vec3 aWater;"
    "uniform mat4 uVP;uniform float uTime;out vec3 vPos;out vec3 vWater;"
    "void main(){vPos=aPos;vWater=aWater;vec2 q=aPos.xz-aWater.yz*uTime;"
    "float moving=smoothstep(.25,.9,length(aWater.yz));"
    "vPos.y+=(sin(dot(q,vec2(.8,.6))+uTime)*.006+"
    "sin(dot(q,vec2(-1.1,.9))-uTime*1.3)*.003)*mix(.4,1.,moving)*smoothstep(0.,.4,aWater.x);"
    "gl_Position=uVP*vec4(vPos,1.);}";
static const char *water_fs[] = {GLSL CLOUD_GLSL
    "in vec3 vPos,vWater;out vec4 frag;"
    "uniform sampler2D uOpaque,uDepth,uShadow;uniform mat4 uVP,uLight;"
    "uniform vec3 uEye,uSky;uniform vec2 uResolution,uDepthParams;"
    "uniform vec4 uViewport,uWakes[8];uniform float uTime,uOvercast;"
    "uniform bool uOrtho;uniform int uTrack;"
    "float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}"
    "float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);"
    "return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),"
    "mix(hash(i+vec2(0,1)),hash(i+1.),f.x),f.y);}"
    // Analytic slopes of smooth noise make irregular wavelets without a tiled
    // sine-wave lattice. Filter the smallest scales before they become subpixel.
    "vec2 slope(vec2 p){vec2 i=floor(p),f=fract(p);"
    "vec2 u=f*f*f*(f*(f*6.-15.)+10.),du=30.*f*f*(f*(f-2.)+1.);"
    "float a=hash(i),b=hash(i+vec2(1,0)),c=hash(i+vec2(0,1)),d=hash(i+1.);"
    "return du*(vec2(b-a,c-a)+(a-b-c+d)*u.yx);}"
    "float distanceAt(float d){float z=d*2.-1.;return uOrtho?"
    "(uDepthParams.y-z)/uDepthParams.x:uDepthParams.y/(z+uDepthParams.x);}"
    "vec2 screenUV(vec4 clip){return (clip.xy/clip.w*.5+.5)*uViewport.zw+uViewport.xy;}"
    "vec2 safeUV(vec2 uv){vec2 px=1./uResolution;"
    "return clamp(uv,uViewport.xy+px,uViewport.xy+uViewport.zw-px);}",
    "void main(){vec2 flow=vWater.yz,q=vPos.xz-flow*uTime;"
    "float sunlight=(1.-.85*uOvercast)*(1.-.82*cloudShade(vPos));"
    "float t=uTime,moving=smoothstep(.25,.9,length(flow));"
    "float footprint=length(fwidth(vPos.xz));"
    "vec2 grad=slope(q*.65+vec2(t*.035,-t*.025))*.018;"
    "grad+=slope(q*1.9+vec2(-t*.07,t*.04)+17.)*.013*"
    "(1.-smoothstep(.2,.65,footprint));"
    "grad+=slope(q*5.3+vec2(t*.11,t*.08)+43.)*.006*"
    "(1.-smoothstep(.06,.24,footprint));"
    "grad*=mix(.65,1.8,moving);"
    "vec2 along=flow/max(length(flow),.001);"
    "float ripple=dot(q,along)*7.+noise(q*.8)*3.;"
    "grad+=along*cos(ripple)*.012*moving*(1.-smoothstep(.1,.4,footprint));"
    "for(int i=0;i<8;i++){vec2 d=vPos.xz-uWakes[i].xy;float r=length(d);"
    "grad+=d/max(r,.2)*sin(r*11.-t*12.)*exp(-r*.8)*uWakes[i].z*.055;}"
    "grad*=mix(.35,1.,smoothstep(0.,.4,vWater.x));"
    "vec3 n=normalize(vec3(-grad.x,1.,-grad.y)),V=normalize(uEye-vPos);"
    "vec2 uv=gl_FragCoord.xy/uResolution;float waterD=distanceAt(gl_FragCoord.z);"
    "float depth=max(0.,distanceAt(texture(uDepth,uv).r)-waterD);"
    "float shore=1.-smoothstep(.025,.24,depth);"
    "vec2 refrUV=safeUV(uv+grad*.025*min(depth,2.));"
    "if(distanceAt(texture(uDepth,refrUV).r)<waterD)refrUV=uv;"
    "vec3 bed=texture(uOpaque,refrUV).rgb;"
    // Extinction follows the light path through the water, not just vertical
    // depth. Shallows reveal the bed; a pond gains body instead of a milky film.
    "float thickness=min(depth,8.);"
    "vec3 tint=uTrack==1?vec3(.055,.24,.22):uTrack==2?vec3(.055,.13,.16):vec3(.065,.16,.135);"
    "vec3 extinction=uTrack==1?vec3(.95,.38,.3):vec3(1.45,.85,.65);"
    "vec3 absorption=exp(-extinction*thickness);"
    "vec3 transmitted=bed*absorption+tint*(1.-absorption);"
    "vec2 cq=q+vec2(noise(q*.7+t*.13),noise(q*.6-t*.11))*1.8;"
    "float caustic=pow(max(0.,1.-abs(sin(cq.x*3.1+sin(cq.y*2.7))+sin(cq.y*3.7+t*.4))*.5),18.);"
    "caustic*=1.-smoothstep(.15,.6,length(fwidth(q)));"
    "transmitted+=bed*caustic*.065*exp(-thickness*.8)*sunlight;"
    "vec3 reflected=reflect(-V,n);float horizon=pow(1.-max(0.,reflected.y),3.);"
    "vec3 sky=mix(uSky*.78,uSky*1.06,horizon);"
    "float cloud=cloudCover(vPos.xz+reflected.xz/max(.18,reflected.y)*(110.-vPos.y));"
    "sky=mix(sky,vec3(.92,.94,.90),cloud*.32*uCloudAmount);"
    // Short screen-space rays reflect nearby banks, trees and cars; sky fills
    // off-screen rays. Depth comes from the opaque scene, never the water itself.
    "vec3 reflection=sky;"
    "if(!uOrtho){for(int i=1;i<=16;i++){float stepD=float(i)*1.2;"
    "vec3 ray=vPos+n*.18+reflected*stepD;vec4 clip=uVP*vec4(ray,1.);"
    "if(clip.w<=0.)break;vec2 sampleUV=screenUV(clip);"
    "if(any(lessThan(sampleUV,uViewport.xy))||any(greaterThan(sampleUV,uViewport.xy+uViewport.zw)))break;"
    "float sceneD=distanceAt(texture(uDepth,sampleUV).r);"
    "float delta=distanceAt(clip.z/clip.w*.5+.5)-sceneD;"
    "if(delta>0.&&delta<.65){vec2 border=min(sampleUV-uViewport.xy,uViewport.xy+uViewport.zw-sampleUV);"
    "float fade=smoothstep(0.,.045,min(border.x,border.y))*(1.-float(i)/20.);"
    "reflection=mix(sky,texture(uOpaque,sampleUV).rgb,fade*.8);break;}}}"
    // Air/water Fresnel reflectance: no constant white varnish at steep views.
    "float fresnel=.0204+.9796*pow(1.-max(0.,dot(n,V)),5.);"
    "vec3 col=mix(transmitted,reflection,fresnel);"
    "vec3 light=normalize(vec3(-.6,1.,.45));vec3 halfV=normalize(light+V);"
    "vec4 sh=uLight*vec4(vPos,1.);vec3 sc=sh.xyz/sh.w*.5+.5;"
    "float lit=1.;if(all(greaterThan(sc,vec3(0)))&&all(lessThan(sc,vec3(1))))"
    "lit=sc.z-.001<texture(uShadow,sc.xy).r?1.:.2;"
    "float glint=pow(max(0.,dot(n,halfV)),300.)*.18+pow(max(0.,dot(n,halfV)),40.)*.04;"
    "col+=vec3(1.,.94,.76)*glint*lit*sunlight;"
    "float foam=shore*smoothstep(.68,.9,noise(q*3.))*moving;"
    "col=mix(col,vec3(.75,.83,.79),foam*.16);"
    "float fog=smoothstep(85.,190.,length(uEye-vPos));if(uOrtho)fog=0.;"
    "col=mix(col,uSky,fog*.88);"
    "frag=vec4(col,smoothstep(0.,.065,depth));}"};
static const char *spray_vs = GLSL
    "layout(location=0)in vec4 aPosition;layout(location=1)in vec4 aMotion;"
    "uniform mat4 uVP;uniform vec3 uRight,uUp;out vec2 vUV;out float vFade,vKind;"
    "void main(){vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),"
    "vec2(-1,-1),vec2(1,1),vec2(-1,1));vUV=corners[gl_VertexID];"
    "vFade=aMotion.x;vKind=aMotion.y;vec3 p=aPosition.xyz;"
    "float size=aPosition.w;"
    "if(vKind>1.5)p+=vec3(vUV.x,0.,vUV.y)*size;"
    "else p+=uRight*vUV.x*size+uUp*vUV.y*size*(1.3+aMotion.z*.12);"
    "gl_Position=uVP*vec4(p,1.);}";
static const char *spray_fs = GLSL
    "in vec2 vUV;in float vFade,vKind;out vec4 frag;"
    "void main(){float d=length(vUV),a;vec3 color;"
    "if(vKind>1.5){a=(1.-smoothstep(.04,.12,abs(d-.78)))*.22;"
    "color=vec3(.76,.88,.85);}else{a=1.-smoothstep(.2,1.,d);"
    "float rim=pow(max(0.,1.-length(vUV-vec2(-.27,.28))*1.5),4.);"
    "color=mix(vec3(.39,.65,.68),vec3(1.),rim+.3);a*=.8;}"
    "a*=vFade;if(a<.008)discard;frag=vec4(color,a);}";
static const char *post_vs =
    GLSL "out vec2 uv;void main(){vec2 "
         "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}";
static const char *post_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uScene;uniform vec2 uResolution;uniform float "
    "uVignette,uSpeedBlur;\n"
    "void main(){vec2 px=1./uResolution;vec3 c=texture(uScene,uv).rgb;vec3 "
    "n=texture(uScene,uv+vec2(0,px.y)).rgb,s=texture(uScene,uv-vec2(0,px.y)).rgb,e=texture(uScene,"
    "uv+vec2(px.x,0)).rgb,w=texture(uScene,uv-vec2(px.x,0)).rgb;vec3 l=vec3(.299,.587,.114);float "
    "hi=max(max(dot(n,l),dot(s,l)),max(dot(e,l),dot(w,l))),lo=min(min(dot(n,l),dot(s,l)),min(dot(e,"
    "l),dot(w,l)));float edge=smoothstep(.055,.22,hi-lo);c=mix(c,(n+s+e+w+c*2.)/6.,edge*.60);vec2 "
    "q=uv*(1.-uv);"
    // A radial trail only at the sides; the road ahead and HUD stay sharp.
    "float blur=uSpeedBlur*smoothstep(.22,.49,abs(uv.x-.5));"
    "if(blur>.0001){vec2 ray=uv-vec2(.5,.58);vec3 trail=c;"
    "for(int i=1;i<=8;i++){vec2 tap=uv-ray*(blur*float(i)/8.);"
    "trail+=texture(uScene,clamp(tap,px*.5,1.-px*.5)).rgb;}"
    "c=mix(c,trail/9.,.85);}float "
    "vign=clamp(pow(16.*q.x*q.y,.22),.65,1.);c*=mix(1.,vign,uVignette);frag=vec4(c,1.);}";

static GLuint render_program_sources(const char *vs, const char *const *fs, int fs_count) {
    GLuint shaders[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    char log[4096];
    for (int i = 0; i < 2; i++) {
        glShaderSource(shaders[i], i ? fs_count : 1, i ? fs : &vs, NULL);
        glCompileShader(shaders[i]);
        GLint ok;
        glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(shaders[i], sizeof log, NULL, log);
            SDL_Log("Shader: %s", log);
            glDeleteShader(shaders[0]);
            glDeleteShader(shaders[1]);
            return 0;
        }
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, shaders[0]);
    glAttachShader(p, shaders[1]);
    glLinkProgram(p);
    glDeleteShader(shaders[0]);
    glDeleteShader(shaders[1]);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        SDL_Log("Program: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}
GLuint render_program(const char *vs, const char *fs) {
    return render_program_sources(vs, &fs, 1);
}
static Mesh mesh_upload_layout(const void *data, int count, bool crowd) {
    Mesh m = {.count = count};
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    size_t stride = crowd ? 18 * sizeof(float) : sizeof(Vertex);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * stride), data, GL_STATIC_DRAW);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i == 3 ? 1 : 3, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                              (void *)(uintptr_t)(i * 3 * sizeof(float)));
    }
    if (crowd)
        for (int i = 4; i < 6; i++) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                                  (void *)(uintptr_t)((10 + (i - 4) * 4) * sizeof(float)));
        }
    glBindVertexArray(0);
    return m;
}
static Mesh mesh_upload(const Vertex *data, int count) {
    return mesh_upload_layout(data, count, false);
}
static bool mesh_load(Mesh *m, const char *path) {
    size_t size;
    unsigned char *d = asset_read(path, &size);
    if (!d || size < 8 || (memcmp(d, "TCM1", 4) && memcmp(d, "TCA1", 4) && memcmp(d, "TCF1", 4) && memcmp(d, "TCV1", 4))) {
        SDL_Log("Cannot load mesh %s", path);
        SDL_free(d);
        return false;
    }
    uint32_t count;
    bool wildlife = !memcmp(d, "TCF1", 4);
    bool vegetation = !memcmp(d, "TCV1", 4);
    bool crowd = !memcmp(d, "TCA1", 4) || wildlife || vegetation;
    size_t floats = crowd ? 18 : 10;
    memcpy(&count, d + 4, 4);
    count = SDL_Swap32LE(count);
    if (count > 5000000 || count % 3 || size != 8 + (size_t)count * floats * sizeof(float)) {
        SDL_free(d);
        return false;
    }
    Vertex *data = (Vertex *)(void *)(d + 8);
    // The format is explicitly little endian, including IEEE754 components.
    for (size_t i = 0; i < (size_t)count * floats; i++) {
        uint32_t bits;
        memcpy(&bits, (unsigned char *)data + i * 4, 4);
        bits = SDL_Swap32LE(bits);
        float f;
        memcpy(&f, &bits, 4);
        if (!isfinite(f)) {
            SDL_free(d);
            return false;
        }
        memcpy((unsigned char *)data + i * 4, &bits, 4);
    }
    if (crowd) {
        const float *values = (const float *)(const void *)data;
        for (uint32_t i = 0; i < count; i++) {
            const float *v = values + i * floats;
            if (vegetation) {
                if (v[13] <= 0 || v[13] > 20 || v[14] < 0 || v[14] > 1 ||
                    (v[15] != 0 && v[15] != 1) || v[16] != 0 || v[17] != 0) {
                    SDL_Log("Invalid vegetation data: %s", path);
                    SDL_free(d);
                    return false;
                }
                continue;
            }
            if (v[14] < 0 || v[14] > 4 || floorf(v[14]) != v[14] || v[15] < 0 ||
                v[15] > 3 || floorf(v[15]) != v[15] ||
                (wildlife ? (fabsf(v[16]) > .21f || fabsf(v[17]) > .21f) :
                 (v[16] < .5f || v[16] > 2 || v[17] < .5f || v[17] > 2))) {
                SDL_Log("Invalid animation data: %s", path);
                SDL_free(d);
                return false;
            }
        }
    }
    *m = mesh_upload_layout(data, (int)count, crowd);
    SDL_free(d);
    return true;
}
static void uniform_matrix(GLuint p, const char *name, Mat4 m) {
    glUniformMatrix4fv(glGetUniformLocation(p, name), 1, GL_FALSE, m.m);
}
static void uniform_vec(GLuint p, const char *name, Vec3 v) {
    glUniform3f(glGetUniformLocation(p, name), v.x, v.y, v.z);
}
static void draw_mesh(Renderer *r, Mesh mesh, Mat4 model, Color paint, bool depth) {
    GLuint p = depth ? r->depth_shader : r->shader;
    uniform_matrix(p, "uModel", model);
    if (!depth)
        glUniform3f(glGetUniformLocation(p, "uPaint"), paint.r, paint.g, paint.b);
    if (r->bound_vao != mesh.vao) {
        glBindVertexArray(mesh.vao);
        r->bound_vao = mesh.vao;
    }
    glDrawArrays(GL_TRIANGLES, 0, mesh.count);
    r->draw_calls++;
    r->triangles += mesh.count / 3;
}
// Surfaces of revolution, centered on the same mass origin as the physics hull.
static Mesh prop_mesh(bool tire) {
    Vertex vertices[24*12*6];
    int count=0, rings=tire ? 12 : 5;
    const float cone_y[]={-.20f,-.02f,.10f,.28f,.40f,.56f};
    for (int ring=0;ring<rings;ring++) for (int side=0;side<24;side++) {
        Vec3 p[4];
        for (int corner=0;corner<4;corner++) {
            float a=(side+(corner==1 || corner==2))*2*PI/24;
            int row=ring+(corner>=2);
            float y,radius;
            if (tire) {
                float b=row*2*PI/12;
                y=.20f*sinf(b); radius=.43f+.20f*cosf(b);
            } else {
                y=cone_y[row]; radius=lerpf(.30f,.035f,(y+.20f)/.76f);
            }
            p[corner]=v3(radius*cosf(a),y,radius*sinf(a));
        }
        int ids[]={0,2,1,0,3,2};
        Color color=tire ? rgb(.065f,.073f,.081f) :
                    (ring==1 || ring==3 ? rgb(.95f,.95f,.88f) : rgb(1,.24f,.025f));
        if (tire && ring%3==0 && side%2==0) color=rgb(.095f,.10f,.11f);
        for (int k=0;k<6;k++) {
            Vec3 n=vnorm(vcross(vsub(p[ids[(k/3)*3+1]],p[ids[(k/3)*3]]),
                                vsub(p[ids[(k/3)*3+2]],p[ids[(k/3)*3]])));
            Vertex v={0}; memcpy(v.p,&p[ids[k]],12); memcpy(v.n,&n,12);
            v.c[0]=color.r; v.c[1]=color.g; v.c[2]=color.b;
            vertices[count++]=v;
        }
    }
    return mesh_upload(vertices,count);
}
// Narrow woven strips leave real openings in the orange safety net.
static void fence_quad(Vertex *vertices,int *count,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Color color) {
    Vec3 points[]={a,b,c,a,c,d};
    Vec3 normal=vnorm(vcross(vsub(b,a),vsub(c,a)));
    for (int i=0;i<6;i++) {
        Vertex v={0}; memcpy(v.p,&points[i],sizeof(Vec3)); memcpy(v.n,&normal,sizeof(Vec3));
        v.c[0]=color.r; v.c[1]=color.g; v.c[2]=color.b;
        vertices[(*count)++]=v;
    }
}
static void draw_fences(Renderer *r,const Game *g,bool depth) {
    Vertex vertices[MAX_FENCE_PANELS*(FENCE_COLS-1)*(FENCE_ROWS-1)*48];
    int count=0;
    for (int i=0;i<g->fence_count;i++) {
        const FencePanel *f=&g->fences[i];
        for (int row=0;row<FENCE_ROWS-1;row++) for (int col=0;col<FENCE_COLS-1;col++) {
            int n=row*FENCE_COLS+col;
            Vec3 a=vlerp(f->nodes[n].render_prev,f->nodes[n].pos,r->alpha);
            Vec3 b=vlerp(f->nodes[n+1].render_prev,f->nodes[n+1].pos,r->alpha);
            Vec3 c=vlerp(f->nodes[n+FENCE_COLS+1].render_prev,f->nodes[n+FENCE_COLS+1].pos,r->alpha);
            Vec3 d=vlerp(f->nodes[n+FENCE_COLS].render_prev,f->nodes[n+FENCE_COLS].pos,r->alpha);
            Color orange=rgb(1,.27f,.035f);
            for (int strand=0;strand<3;strand++) {
                float u=strand/3.f, v=u+.055f;
                fence_quad(vertices,&count,vlerp(a,b,u),vlerp(a,b,v),vlerp(d,c,v),vlerp(d,c,u),orange);
                fence_quad(vertices,&count,vlerp(a,d,u),vlerp(b,c,u),vlerp(b,c,v),vlerp(a,d,v),orange);
            }
            // Reinforced top and bottom hems.
            if (row==0) fence_quad(vertices,&count,a,b,vlerp(b,c,.08f),vlerp(a,d,.08f),orange);
            if (row==FENCE_ROWS-2) fence_quad(vertices,&count,vlerp(a,d,.90f),vlerp(b,c,.90f),c,d,orange);
        }
    }
    if (!count) return;
    glBindBuffer(GL_ARRAY_BUFFER,r->fence.vbo);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(count*sizeof(Vertex)),vertices,GL_STREAM_DRAW);
    r->fence.count=count;
    GLboolean culling=glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    draw_mesh(r,r->fence,identity(),rgb(1,1,1),depth);
    if (culling) glEnable(GL_CULL_FACE);
}
static void draw_props(Renderer *r, const Game *g, bool depth) {
    Color white=rgb(1,1,1);
    for (int i=0;i<g->prop_count;i++) {
        const TrackProp *p=&g->props[i];
        Mat4 rotation=p->rotation;
        for (int k=0;k<12;k++) rotation.m[k]=lerpf(p->prev_rotation.m[k],p->rotation.m[k],r->alpha);
        Mat4 model=mmul(translate(vlerp(p->prev,p->pos,r->alpha)),rotation);
        if (p->kind==PROP_POST) {
            draw_mesh(r,r->cube,mmul(model,scale3(.045f,.65f,.045f)),rgb(.88f,.86f,.73f),depth);
            draw_mesh(r,r->cube,mmul(model,mmul(translate(v3(0,.60f,0)),scale3(.06f,.065f,.06f))),rgb(1,.29f,.03f),depth);
            continue;
        }
        draw_mesh(r,p->kind==PROP_CONE ? r->cone : r->tire,model,white,depth);
        if (p->kind==PROP_CONE) {
            Mat4 base=mmul(model,mmul(translate(v3(0,-.20f,0)),scale3(.36f,.09f,.36f)));
            draw_mesh(r,r->cube,base,rgb(.12f,.13f,.14f),depth);
        }
    }
    draw_fences(r,g,depth);
    // Irregular exposed asphalt/gravel patch inside the cone perimeter.
    for (int i=0;i<7;i++) {
        float a=i*2*PI/7;
        Vec3 local=v3(.28f*cosf(a),0,1.12f*sinf(a));
        float yaw=g->roadwork_yaw;
        Vec3 p=vadd(g->roadwork,v3(cosf(yaw)*local.x+sinf(yaw)*local.z,0,
                                   -sinf(yaw)*local.x+cosf(yaw)*local.z));
        p.y=track_height(&g->tracks[g->selected],p,NULL)+.015f;
        Mat4 model=mmul(translate(p),mmul(rotate_y(yaw+a*.15f),scale3(.38f,.018f,.58f)));
        draw_mesh(r,r->cube,model,i%2 ? rgb(.18f,.16f,.13f) : rgb(.10f,.105f,.11f),depth);
    }
}
static void create_primitives(Renderer *r) {
    Vertex plane[6] = {0};
    Vec3 p[6] = {{-320, -8, 320}, {320, -8, 320},  {320, -8, -320},
                 {-320, -8, 320}, {320, -8, -320}, {-320, -8, -320}};
    for (int i = 0; i < 6; i++) {
        memcpy(plane[i].p, &p[i], 12);
        plane[i].n[1] = 1;
        plane[i].c[0] = .90f;
        plane[i].c[1] = .89f;
        plane[i].c[2] = .85f;
        plane[i].paint = 1;
    }
    r->plane = mesh_upload(plane, 6);
    Vertex cube[36] = {0};
    int n = 0;
    Vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Vec3 corners[6][4] = {{{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},
                                {{-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}},
                                {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
                                {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
                                {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
                                {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}}};
    int inds[] = {0, 1, 2, 0, 2, 3};
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 6; k++) {
            memcpy(cube[n].p, &corners[f][inds[k]], 12);
            memcpy(cube[n].n, &normals[f], 12);
            cube[n].c[0] = cube[n].c[1] = cube[n].c[2] = 1;
            cube[n].paint = 1;
            n++;
        }
    r->cube = mesh_upload(cube, 36);
    r->cone = prop_mesh(false);
    r->tire = prop_mesh(true);
    r->fence = mesh_upload(NULL,0);
}

bool renderer_init(Renderer *r) {
    memset(r, 0, sizeof *r);
    r->last_track = -1;
    r->shader = render_program(world_vs, world_fs);
    r->depth_shader = render_program(depth_vs, depth_fs);
    r->post_shader = render_program(post_vs, post_fs);
    r->weather_shader = render_program(weather_vs, weather_fs);
    r->water_shader = render_program_sources(water_vs, water_fs, SDL_arraysize(water_fs));
    r->spray_shader = render_program(spray_vs, spray_fs);
    if (!r->shader || !r->depth_shader || !r->post_shader || !r->weather_shader ||
        !r->water_shader || !r->spray_shader)
        return false;
    const char *names[] = {"tracks/country.tcm", "tracks/beach.tcm", "tracks/winter.tcm"};
    for (int i = 0; i < 3; i++)
        if (!mesh_load(&r->tracks[i], names[i]))
            return false;
    const char *vegetation[] = {"tracks/country-vegetation.tcv", "tracks/beach-vegetation.tcv",
                                "tracks/winter-vegetation.tcv"};
    for (int i = 0; i < TRACK_COUNT; i++)
        if (!mesh_load(&r->vegetation[i], vegetation[i])) return false;
    const char *wildlife[] = {"tracks/country-wildlife.tcf", "tracks/beach-wildlife.tcf",
                              "tracks/winter-wildlife.tcf"};
    for (int i = 0; i < TRACK_COUNT; i++)
        if (!mesh_load(&r->wildlife[i], wildlife[i])) return false;
    const char *crowds[] = {"tracks/country-crowd.tca", "tracks/beach-crowd.tca",
                            "tracks/winter-crowd.tca"};
    for (int i = 0; i < 3; i++)
        if (!mesh_load(&r->crowds[i], crowds[i]))
            return false;
    if (!mesh_load(&r->car, "models/car.tcm") || !mesh_load(&r->fuel, "models/fuel.tcm"))
        return false;
    if (!mesh_load(&r->helicopter, "models/helicopter.tcm") ||
        !mesh_load(&r->helicopter_rotor, "models/helicopter-rotor.tcm") ||
        !mesh_load(&r->helicopter_tail_rotor, "models/helicopter-tail-rotor.tcm"))
        return false;
    const char *water[] = {"tracks/country-water.tcm", "tracks/beach-water.tcm", "tracks/winter-water.tcm"};
    for (int i = 0; i < TRACK_COUNT; i++)
        if (!mesh_load(&r->water[i], water[i])) return false;
    glGenVertexArrays(1, &r->spray_vao);
    glBindVertexArray(r->spray_vao);
    glGenBuffers(1, &r->spray_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->spray_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 8 * sizeof(float), NULL, GL_STREAM_DRAW);
    for (int i = 0; i < 2; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              (void *)(uintptr_t)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(i, 1);
    }
    create_primitives(r);
    glGenVertexArrays(1, &r->empty_vao);
    glGenFramebuffers(1, &r->shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
    glGenTextures(1, &r->shadow_texture);
    glBindTexture(GL_TEXTURE_2D, r->shadow_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->shadow_texture,
                           0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("Shadow framebuffer incomplete");
        return false;
    }
    glGenFramebuffers(1, &r->scene_fbo);
    glGenTextures(1, &r->scene_texture);
    glGenRenderbuffers(1, &r->scene_depth);
    glGenFramebuffers(1, &r->opaque_fbo);
    glGenTextures(1, &r->opaque_texture);
    glGenTextures(1, &r->opaque_depth);
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
    return true;
}
void renderer_resize(Renderer *r, int w, int h) {
    if (w == r->width && h == r->height)
        return;
    r->width = w;
    r->height = h;
    glBindTexture(GL_TEXTURE_2D, r->scene_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindRenderbuffer(GL_RENDERBUFFER, r->scene_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->scene_texture,
                           0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, r->scene_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("Scene framebuffer incomplete");
    glBindTexture(GL_TEXTURE_2D, r->opaque_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, r->opaque_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, r->opaque_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->opaque_texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, r->opaque_depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("Water refraction framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
}

static void update_helicopter(Renderer *r, const Game *g, float dt) {
    const Track *t = &g->tracks[g->selected];
    // A broad patrol brings the camera ship alongside briefly every 48 seconds.
    // Follow the race's track position, never the chase camera or car steering.
    float phase = (g->time - 14.f) * (2 * PI / 48.f);
    float lane = 78.f - 56.f * cosf(phase);
    Vec3 direction;
    Vec3 p = track_at(t, g->cars[0].last_s + 38.f + 25.f * sinf(phase), lane, &direction);
    p.y = fmaxf(p.y, track_height(t, p, NULL)) + 19.f + 4.f * (1 - cosf(phase));
    float yaw = atan2f(direction.x, direction.z);
    if (!r->helicopter_ready || r->last_track != g->selected ||
        g->time < r->helicopter_race_time || r->last_screen == SCREEN_MENU) {
        r->helicopter_pos = p;
        r->helicopter_yaw = yaw;
        r->helicopter_ready = true;
    } else if (g->screen == SCREEN_RACE) {
        float blend = 1 - expf(-clampf(dt, 0, .1f) * 1.2f);
        r->helicopter_pos = vlerp(r->helicopter_pos, p, blend);
        r->helicopter_yaw += angle_delta(yaw, r->helicopter_yaw) * blend;
    }
    r->helicopter_race_time = g->time;
}

// Cache surface-following chevrons as one mesh per circuit.
static void draw_boosts(Renderer *r, const Game *g, bool depth) {
    const Track *t = &g->tracks[g->selected];
    Mesh *mesh = &r->boosts[g->selected];
    if (!mesh->vao) {
        Vertex vertices[MAX_BOOSTS * 12 * 16 * 6] = {0};
        int count = 0;
        const int indices[] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < t->boost_count; i++) {
            for (int row = 0; row < 12; row++) {
                float s = t->boosts[i] - BOOST_LENGTH * .5f + row * .5f;
                for (int col = 0; col < 16; col++) {
                    float lane = -BOOST_HALF + col * BOOST_HALF / 8;
                    Vec3 corners[4] = {
                        track_at(t, s, lane, NULL),
                        track_at(t, s + .5f, lane, NULL),
                        track_at(t, s + .5f, lane + BOOST_HALF / 8, NULL),
                        track_at(t, s, lane + BOOST_HALF / 8, NULL)};
                    for (int k = 0; k < 4; k++)
                        corners[k].y = track_height(t, corners[k], NULL) + .045f;
                    Vec3 normal = vnorm(vcross(vsub(corners[1], corners[0]),
                                              vsub(corners[2], corners[0])));
                    for (int k = 0; k < 6; k++) {
                        Vertex *v = &vertices[count++];
                        memcpy(v->p, &corners[indices[k]], sizeof(Vec3));
                        memcpy(v->n, &normal, sizeof(Vec3));
                        // Reuse vertex colors for pad-local coordinates in the boost shader.
                        int corner = indices[k];
                        v->c[0] = lane + (corner >= 2 ? BOOST_HALF / 8 : 0);
                        v->c[1] = row * .5f + (corner == 1 || corner == 2 ? .5f : 0);
                    }
                }
            }
        }
        *mesh = mesh_upload(vertices, count);
        r->bound_vao = 0;
    }
    if (!depth) {
        glUniform1i(glGetUniformLocation(r->shader, "uBoost"), 1);
        glUniform1f(glGetUniformLocation(r->shader, "uBoostTime"),
                    g->screen == SCREEN_MENU ? r->crowd_time : g->weather.time);
    }
    draw_mesh(r, *mesh, identity(), rgb(1,1,1), depth);
    if (!depth) glUniform1i(glGetUniformLocation(r->shader, "uBoost"), 0);
}

static void draw_helicopter(Renderer *r, const Game *g, bool depth) {
    float time = g->time;
    Mat4 model = mmul(translate(vadd(r->helicopter_pos, v3(0, .25f * sinf(time * 1.3f), 0))),
                      mmul(rotate_y(r->helicopter_yaw),
                           mmul(rotate_x(-.045f), rotate_z(.06f * sinf(time * .3f)))));
    Color white = rgb(1, 1, 1);
    draw_mesh(r, r->helicopter, model, white, depth);
    Mat4 rotor = mmul(model, mmul(translate(v3(0, 1.55f, 0)), rotate_y(time * 37.f)));
    draw_mesh(r, r->helicopter_rotor, rotor, white, depth);
    Mat4 tail = mmul(model, mmul(translate(v3(.25f, .74f, -4.02f)), rotate_x(time * 53.f)));
    draw_mesh(r, r->helicopter_tail_rotor, tail, white, depth);
}

static void render_objects(Renderer *r, const Game *g, bool depth, bool menu, bool garage) {
    Color white = rgb(1, 1, 1);
    Track const *t = &g->tracks[g->selected];
    if (garage) {
        Mat4 platform = mmul(translate(v3(0, -.18f, 0)), scale3(3.5f, .16f, 3.5f));
        draw_mesh(r, r->cube, platform, rgb(.89, .87, .80), depth);
        Mat4 model = mmul(rotate_y(g->clock * .20f + .5f), scale3(1.65f, 1.65f, 1.65f));
        draw_mesh(r, r->car, model, CAR_COLORS[g->profile.color], depth);
        return;
    }
    draw_mesh(r, r->tracks[g->selected], identity(), white, depth);
    draw_boosts(r, g, depth);
    GLuint program = depth ? r->depth_shader : r->shader;
    glUniform1i(glGetUniformLocation(program, "uVegetation"), 1);
    // A persistent breeze keeps clear spells alive. Weather adds smoothly fading
    // gusts; a fixed base direction avoids jumps when a new weather spell starts.
    Vec3 vegetation_wind = v3(1.6f, 0, .8f);
    if (!menu) vegetation_wind = vadd(vegetation_wind, g->weather.wind);
    uniform_vec(program, "uVegetationWind", vegetation_wind);
    glUniform1f(glGetUniformLocation(program, "uVegetationTime"),
                menu ? r->crowd_time : g->weather.time);
    draw_mesh(r, r->vegetation[g->selected], identity(), white, depth);
    glUniform1i(glGetUniformLocation(program, "uVegetation"), 0);
    glUniform1i(glGetUniformLocation(program, "uCrowd"), 1);
    glUniform1f(glGetUniformLocation(program, "uCrowdTime"), r->crowd_time);
    glUniform1f(glGetUniformLocation(program, "uWind"), menu ? 0 : vlen(g->weather.wind));
    draw_mesh(r, r->crowds[g->selected], identity(), white, depth);
    glUniform1i(glGetUniformLocation(program, "uCrowd"), 0);
    glUniform1i(glGetUniformLocation(program, "uWildlife"), 1);
    draw_mesh(r, r->wildlife[g->selected], identity(), white, depth);
    glUniform1i(glGetUniformLocation(program, "uWildlife"), 0);
    if (!menu)
        draw_props(r, g, depth);
    if (!menu)
        draw_helicopter(r, g, depth);
    if (!depth && !menu)
        draw_mesh(r, r->plane, identity(), t->sky, depth);
    for (int i = 0; i < CAR_COUNT; i++) {
        Vec3 pos;
        float yaw, pitch = 0, roll = 0;
        Color color;
        if (menu) {
            Vec3 d;
            pos = track_at(t, g->clock * 13.f - i * 12 + 170, (i % 2 ? 1 : -1) * 1.7f, &d);
            yaw = atan2f(d.x, d.z);
            car_surface_pose(t, &pos, yaw, &pitch, &roll);
            color = CAR_COLORS[(i + g->profile.color) % 6];
        } else {
            const Car *c = &g->cars[i];
            // Use simulation time so blinking freezes with the game, including shadows.
            if (c->reset_protection > 0 && ((int)(c->reset_protection * 8) % 2) != 0)
                continue;
            pos = vlerp(c->prev, c->pos, r->alpha);
            yaw = c->prev_yaw + angle_delta(c->yaw, c->prev_yaw) * r->alpha;
            pitch = c->pitch;
            roll = c->roll;
            color = c->color;
        }
        // The modeled tire bottoms are .04 below the car origin.
        Mat4 model = mmul(translate(vadd(pos, v3(0, .04f, 0))),
                          mmul(rotate_y(yaw), mmul(rotate_x(pitch), rotate_z(roll))));
        if (!menu && g->cars[i].tumbling) {
            const Car *c = &g->cars[i];
            Mat4 rotation = c->body_rotation;
            for (int k = 0; k < 12; k++)
                rotation.m[k] = lerpf(c->prev_body_rotation.m[k], rotation.m[k], r->alpha);
            model = mmul(translate(vadd(pos, v3(0, .04f, 0))), rotation);
        }
        draw_mesh(r, r->car, model, color, depth);
    }
    for (int i = 0; i < t->can_count; i++) {
        if (!menu && g->cars[0].can_cooldown[i] > 0)
            continue;
        Can can = t->cans[i];
        Vec3 p = track_at(t, t->points[(int)can.index].s, can.lane, NULL);
        if (!menu && vlen(vsub(p, r->target)) > 90)
            continue;
        Mat4 model = mmul(translate(vadd(p, v3(0, .7f + sinf(g->clock * 3 + i) * .22f, 0))),
                          mmul(rotate_y(g->clock * 1.5f + i), scale3(1.4f, 1.4f, 1.4f)));
        draw_mesh(r, r->fuel, model, white, depth);
    }
    if (!depth && !menu) {
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), .30f);
        for (int i = 0; i < MAX_PARTICLES; i++) {
            const Particle *p = &g->particles[i];
            if (p->life <= 0 || p->kind != PARTICLE_DUST)
                continue;
            float s = p->size * (.3f + .7f * p->life / p->max_life);
            Mat4 model = mmul(translate(p->pos), mmul(rotate_y(p->life * 2), scale3(s, s, s)));
            draw_mesh(r, r->cube, model, p->color, false);
        }
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    }
}

static void draw_weather(Renderer *r, const Game *g) {
    const Weather *w = &g->weather;
    if (w->intensity <= 0 || w->kind == WEATHER_CLEAR)
        return;
    GLuint p = r->weather_shader;
    glUseProgram(p);
    uniform_matrix(p, "uVP", r->vp);
    uniform_vec(p, "uCenter", r->cam_pos);
    uniform_vec(p, "uRight", v3(r->view.m[0], r->view.m[4], r->view.m[8]));
    uniform_vec(p, "uUp", v3(r->view.m[1], r->view.m[5], r->view.m[9]));
    uniform_vec(p, "uDrift", w->drift);
    uniform_vec(p, "uWind", w->wind);
    glUniform1f(glGetUniformLocation(p, "uTime"), w->time);
    glUniform1f(glGetUniformLocation(p, "uIntensity"), w->intensity);
    glUniform1i(glGetUniformLocation(p, "uKind"), w->kind);
    glUniform1i(glGetUniformLocation(p, "uTrack"), g->selected);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(r->empty_vao);
    int count = w->kind == WEATHER_WIND ? 300 : 1800;
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    r->draw_calls++;
    r->triangles += count * 2;
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    r->bound_vao = 0;
}

static void cloud_uniforms(GLuint program, const Renderer *r, const Game *g, bool overview,
                           bool garage) {
    Vec3 drift = overview ? v3(0, 0, 0) : g->weather.drift;
    glUniform2f(glGetUniformLocation(program, "uCloudOffset"),
                r->crowd_time * .85f + drift.x * .18f,
                r->crowd_time * .32f + drift.z * .18f);
    // Coastal fair weather has lighter cloud shadows than the alpine circuit.
    float amount = g->selected == 1 ? .68f : g->selected == 2 ? .90f : .82f;
    glUniform1f(glGetUniformLocation(program, "uCloudAmount"), garage ? 0 : amount);
}

static void draw_water(Renderer *r, const Game *g, bool overview, Color sky, float overcast,
                       int vx, int vy, int vw, int vh) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->scene_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, r->opaque_fbo);
    glBlitFramebuffer(0, 0, r->width, r->height, 0, 0, r->width, r->height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    GLuint p = r->water_shader;
    glUseProgram(p);
    uniform_matrix(p, "uVP", r->vp);
    uniform_matrix(p, "uLight", r->light_vp);
    uniform_vec(p, "uEye", r->eye);
    uniform_vec(p, "uSky", v3(sky.r, sky.g, sky.b));
    cloud_uniforms(p, r, g, overview, false);
    glUniform1f(glGetUniformLocation(p, "uTime"), r->crowd_time);
    glUniform1f(glGetUniformLocation(p, "uOvercast"), overcast);
    glUniform1i(glGetUniformLocation(p, "uOrtho"), overview);
    glUniform1i(glGetUniformLocation(p, "uTrack"), g->selected);
    glUniform2f(glGetUniformLocation(p, "uResolution"), r->width, r->height);
    glUniform2f(glGetUniformLocation(p, "uDepthParams"), r->projection.m[10], r->projection.m[14]);
    glUniform4f(glGetUniformLocation(p, "uViewport"), (float)vx/r->width, (float)vy/r->height,
                (float)vw/r->width, (float)vh/r->height);
    float wakes[CAR_COUNT * 4] = {0};
    for (int i = 0; i < CAR_COUNT && !overview; i++) {
        const Car *c = &g->cars[i];
        float level;
        wakes[i*4] = c->pos.x; wakes[i*4+1] = c->pos.z;
        if (c->grounded && track_water(&g->tracks[g->selected], c->pos, &level) &&
            level > c->pos.y && level < c->pos.y + 1)
            wakes[i*4+2] = clampf(c->speed / 18, 0, 1);
    }
    glUniform4fv(glGetUniformLocation(p, "uWakes[0]"), CAR_COUNT, wakes);
    GLuint textures[] = {r->opaque_texture, r->opaque_depth, r->shadow_texture};
    const char *names[] = {"uOpaque", "uDepth", "uShadow"};
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glUniform1i(glGetUniformLocation(p, names[i]), i);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    Mesh m = r->water[g->selected];
    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, 0, m.count);
    r->draw_calls++; r->triangles += m.count / 3;
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    r->bound_vao = 0;
}

static void draw_spray(Renderer *r, const Game *g) {
    float data[MAX_PARTICLES * 8];
    int count = 0;
    // Additive-looking highlights use ordinary alpha, sorted back to front so
    // overlapping droplets retain their soft translucent edges.
    int order[MAX_PARTICLES]; float distances[MAX_PARTICLES];
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *q = &g->particles[i];
        if (q->life <= 0 || q->kind == PARTICLE_DUST) continue;
        Vec3 d = vsub(q->pos, r->eye); float distance = vdot(d,d);
        int j = count++;
        while (j > 0 && distances[j-1] < distance) {
            order[j] = order[j-1]; distances[j] = distances[j-1]; j--;
        }
        order[j] = i; distances[j] = distance;
    }
    if (!count) return;
    for (int i = 0; i < count; i++) {
        const Particle *q = &g->particles[order[i]];
        float age = 1 - q->life / q->max_life;
        float size = q->size * (q->kind == PARTICLE_RIPPLE ? 1 + age * 3 : 1 - age * .45f);
        // A ring must stay on the water. Bound its outer radius against the
        // authored shoreline instead of letting it expand over the dry road.
        if (q->kind == PARTICLE_RIPPLE) {
            for (int attempt = 0; attempt < 5; attempt++) {
                bool contained = true;
                for (int edge = 0; edge < 8; edge++) {
                    float angle = edge * PI / 4;
                    Vec3 rim = vadd(q->pos, v3(cosf(angle) * size, 0, sinf(angle) * size));
                    float level;
                    if (!track_water(&g->tracks[g->selected], rim, &level) ||
                        fabsf(level - q->pos.y) > .12f) {
                        contained = false;
                        break;
                    }
                }
                if (contained) break;
                size *= .6f;
            }
        }
        float v[] = {q->pos.x,q->pos.y,q->pos.z,size,1-age,(float)q->kind,fabsf(q->vel.y),0};
        memcpy(data+i*8, v, sizeof v);
    }
    GLuint p = r->spray_shader;
    glUseProgram(p);
    uniform_matrix(p, "uVP", r->vp);
    uniform_vec(p, "uRight", v3(r->view.m[0],r->view.m[4],r->view.m[8]));
    uniform_vec(p, "uUp", v3(r->view.m[1],r->view.m[5],r->view.m[9]));
    glBindVertexArray(r->spray_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->spray_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, count * 8 * sizeof(float), data);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    r->draw_calls++; r->triangles += count * 2;
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    r->bound_vao = 0;
}

void renderer_draw(Renderer *r, const Game *g, float dt, float alpha) {
    if (g->screen != SCREEN_PAUSE)
        r->crowd_time += clampf(dt, 0, .1f);
    bool menu =
        g->screen == SCREEN_MENU || g->screen == SCREEN_HELP || g->screen == SCREEN_SETTINGS ||
        g->screen == SCREEN_RECORDS || g->screen == SCREEN_RESET_DATA;
    bool garage = g->screen == SCREEN_GARAGE;
    bool overview = menu || garage;
    if (!overview)
        update_helicopter(r, g, dt);
    else
        r->helicopter_ready = false;
    const Track *t = &g->tracks[g->selected];
    r->alpha = alpha;
    r->draw_calls = 0;
    r->triangles = 0;
    r->bound_vao = 0;
    Color clear = overview ? rgb(.956f, .949f, .922f) : t->sky;
    float overcast = overview || g->weather.kind == WEATHER_WIND ? 0 : g->weather.intensity;
    clear.r = lerpf(clear.r, .53f, overcast * .75f);
    clear.g = lerpf(clear.g, .61f, overcast * .75f);
    clear.b = lerpf(clear.b, .69f, overcast * .75f);
    int vx = 0, vy = 0, vw = r->width, vh = r->height;
    if (overview) {
        vx = (int)(r->width * .37f);
        vy = (int)(r->height * .265f);
        vw = r->width - vx;
        vh = (int)(r->height * .645f);
    }
    float aspect = (float)vw / vh;
    if (garage) {
        r->eye = v3(7.9f, 6.1f, 10);
        r->target = v3(0, .7f, 0);
        r->projection = perspective(38 * PI / 180, aspect, .1f, 100);
    } else if (menu) {
        float a = .56f + sinf(g->clock * .035f) * .065f;
        r->eye = v3(sinf(a) * 245, 228, cosf(a) * 245);
        r->target = v3(0, 0, 0);
        float span = 150;
        r->projection = ortho(-span * aspect, span * aspect, -span, span, 1, 800);
    } else {
        const Car *c = &g->cars[0];
        Vec3 p = vlerp(c->prev, c->pos, alpha);
        if (!r->camera_ready || r->last_track != g->selected || r->last_screen == SCREEN_MENU) {
            r->camera_yaw = c->yaw;
            r->cam_pos = p;
            r->camera_ready = true;
        }
        r->camera_yaw += angle_delta(c->yaw, r->camera_yaw) * (1 - expf(-dt * 3.8f));
        r->cam_pos = vlerp(r->cam_pos, p, 1 - expf(-dt * 8));
        Vec3 forward = v3(sinf(r->camera_yaw), 0, cosf(r->camera_yaw));
        r->target = vadd(r->cam_pos, vmul(forward, 9 + c->speed * .17f));
        r->eye = vadd(r->cam_pos,
                      vadd(vmul(forward, -23 - c->speed * .15f), v3(0, 32 + c->speed * .25f, 0)));
        if (g->shake > 0)
            r->eye.x += sinf(g->clock * 95) * g->shake * .8f;
        r->projection = perspective(49 * PI / 180, aspect, .5f, 300);
    }
    r->view = lookat(r->eye, r->target, v3(0, 1, 0));
    r->vp = mmul(r->projection, r->view);
    Vec3 light_target = overview ? v3(0, 0, 0) : r->target;
    float shadow_span = garage ? 9 : menu ? 190 : 65;
    r->light_vp = mmul(ortho(-shadow_span, shadow_span, -shadow_span, shadow_span, 1, 650),
                       lookat(vadd(light_target, v3(-180, 300, 135)), light_target, v3(0, 1, 0)));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
    glViewport(0, 0, 2048, 2048);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(r->depth_shader);
    uniform_matrix(r->depth_shader, "uVP", r->light_vp);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2, 3);
    render_objects(r, g, true, menu, garage);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
    glViewport(0, 0, r->width, r->height);
    glClearColor(clear.r, clear.g, clear.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(vx, vy, vw, vh);
    glUseProgram(r->shader);
    uniform_matrix(r->shader, "uVP", r->vp);
    uniform_matrix(r->shader, "uLight", r->light_vp);
    uniform_vec(r->shader, "uEye", r->eye);
    cloud_uniforms(r->shader, r, g, overview, garage);
    uniform_vec(r->shader, "uFog", v3(clear.r, clear.g, clear.b));
    glUniform1f(glGetUniformLocation(r->shader, "uFogFar"), overview ? 1400 : 190 - 65 * overcast);
    glUniform1f(glGetUniformLocation(r->shader, "uOvercast"), overcast);
    glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->shadow_texture);
    glUniform1i(glGetUniformLocation(r->shader, "uShadow"), 0);
    render_objects(r, g, false, menu, garage);
    if (!garage) draw_water(r, g, overview, clear, overcast, vx, vy, vw, vh);
    if (!overview) {
        draw_spray(r, g);
        draw_weather(r, g);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
    glViewport(0, 0, r->width, r->height);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(r->post_shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->scene_texture);
    glUniform1i(glGetUniformLocation(r->post_shader, "uScene"), 0);
    glUniform2f(glGetUniformLocation(r->post_shader, "uResolution"), (float)r->width,
                (float)r->height);
    glUniform1f(glGetUniformLocation(r->post_shader, "uVignette"), overview ? 0 : .3f);
    const Car *player = &g->cars[0];
    float pace = clampf((player->speed - 16.f) / 24.f, 0, 1);
    pace = pace * pace * (3 - 2 * pace);
    float blur = g->screen == SCREEN_RACE && !player->dnf && !player->finished
                     ? pace * (.065f + .025f * clampf(player->boost_cooldown / 2, 0, 1)) : 0;
    glUniform1f(glGetUniformLocation(r->post_shader, "uSpeedBlur"), blur);
    glBindVertexArray(r->empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    r->last_screen = g->screen;
    r->last_track = g->selected;
}

bool renderer_project(const Renderer *r, Vec3 p, float *x, float *y) {
    float xx = r->vp.m[0] * p.x + r->vp.m[4] * p.y + r->vp.m[8] * p.z + r->vp.m[12];
    float yy = r->vp.m[1] * p.x + r->vp.m[5] * p.y + r->vp.m[9] * p.z + r->vp.m[13];
    float w = r->vp.m[3] * p.x + r->vp.m[7] * p.y + r->vp.m[11] * p.z + r->vp.m[15];
    if (w <= 0)
        return false;
    *x = (xx / w * .5f + .5f) * r->ui_width;
    *y = (.5f - yy / w * .5f) * r->ui_height;
    return true;
}
bool renderer_screenshot(const Renderer *r, const char *path) {
    size_t bytes = (size_t)r->width * r->height * 4;
    unsigned char *p = SDL_malloc(bytes), *flip = SDL_malloc(bytes);
    if (!p || !flip) {
        SDL_free(p);
        SDL_free(flip);
        return false;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, r->width, r->height, GL_RGBA, GL_UNSIGNED_BYTE, p);
    for (int y = 0; y < r->height; y++)
        memcpy(flip + (size_t)y * r->width * 4, p + (size_t)(r->height - 1 - y) * r->width * 4,
               (size_t)r->width * 4);
    SDL_Surface *s =
        SDL_CreateSurfaceFrom(r->width, r->height, SDL_PIXELFORMAT_RGBA32, flip, r->width * 4);
    bool ok = s && SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    SDL_free(p);
    SDL_free(flip);
    return ok;
}
static void mesh_delete(Mesh *m) {
    glDeleteBuffers(1, &m->vbo);
    glDeleteVertexArrays(1, &m->vao);
}
void renderer_destroy(Renderer *r) {
    for (int i = 0; i < 3; i++) {
        mesh_delete(&r->tracks[i]);
        mesh_delete(&r->boosts[i]);
        mesh_delete(&r->crowds[i]);
        mesh_delete(&r->wildlife[i]);
        mesh_delete(&r->vegetation[i]);
        mesh_delete(&r->water[i]);
    }
    mesh_delete(&r->cone);
    mesh_delete(&r->tire);
    mesh_delete(&r->fence);
    mesh_delete(&r->car);
    mesh_delete(&r->fuel);
    mesh_delete(&r->helicopter);
    mesh_delete(&r->helicopter_rotor);
    mesh_delete(&r->helicopter_tail_rotor);
    mesh_delete(&r->plane);
    mesh_delete(&r->cube);
    glDeleteProgram(r->shader);
    glDeleteProgram(r->depth_shader);
    glDeleteProgram(r->post_shader);
    glDeleteProgram(r->weather_shader);
    glDeleteProgram(r->water_shader);
    glDeleteProgram(r->spray_shader);
    glDeleteVertexArrays(1, &r->spray_vao);
    glDeleteBuffers(1, &r->spray_vbo);
    glDeleteTextures(1, &r->opaque_texture);
    glDeleteTextures(1, &r->opaque_depth);
    glDeleteFramebuffers(1, &r->opaque_fbo);
    glDeleteVertexArrays(1, &r->empty_vao);
    glDeleteTextures(1, &r->shadow_texture);
    glDeleteTextures(1, &r->scene_texture);
    glDeleteFramebuffers(1, &r->shadow_fbo);
    glDeleteFramebuffers(1, &r->scene_fbo);
    glDeleteRenderbuffers(1, &r->scene_depth);
}
