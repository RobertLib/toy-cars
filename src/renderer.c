#include "renderer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Lens centres of each car's headlamps and tail lamps, as modeled in
// tools/build_assets.py: the right lamp mirrors the left one across the car.
static const struct {
    Vec3 head, tail, half[2];
} CAR_LAMPS[CAR_MODEL_COUNT] = {
    [CAR_TRAILBLAZER] = {{.57f, .80f, 1.525f}, {.57f, .73f, -1.522f}, {{.15f, .08f, .01f}, {.16f, .055f, .01f}}},
    [CAR_SPRITE] = {{.55f, .74f, 1.335f}, {.60f, .82f, -1.341f}, {{.13f, .10f, .01f}, {.09f, .13f, .01f}}},
    [CAR_BRUISER] = {{.60f, .98f, 1.600f}, {.72f, 1.02f, -1.610f}, {{.14f, .08f, .01f}, {.08f, .09f, .01f}}},
    [CAR_COMET] = {{.62f, .60f, 1.615f}, {.55f, .62f, -1.630f}, {{.18f, .045f, .01f}, {.21f, .04f, .01f}}},
};
// Lamps 0 and 1 are the left and right headlamps, 2 and 3 the tail lamps.
static Vec3 car_lamp(int model, int lamp) {
    Vec3 p = lamp >= 2 ? CAR_LAMPS[model].tail : CAR_LAMPS[model].head;
    p.x *= lamp % 2 ? 1 : -1;
    return p;
}
// Night halos reserved for the railway's signal lamps, arm tips and headlamps.
#define RAIL_HALOS 16
typedef struct {
    float p[3], n[3], c[3], paint;
} Vertex;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define GLSL                                                                                       \
    "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" \
    "precision highp sampler2DShadow;\n"
// Phones take fewer occlusion samples and depth-of-field taps.
#define QUALITY "#define AO_SAMPLES 8\n#define DOF_TAPS 16\n"
#else
#define GLSL "#version 330 core\n"
#define QUALITY "#define AO_SAMPLES 12\n#define DOF_TAPS 24\n"
#endif
// Shared by the color and shadow passes so limbs and their shadows agree.
// TCA1: world origin/yaw, pose, body part, scale and width.
// TCF1: world origin, the animal's size relative to a deer (0 for a deer), species,
// body part and the verified ground slopes; the size scales the grazing nod's pivot.
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
    "vec3 pivot=vec3(0.,1.28,.5)*(1.+aOrigin.w);p=pivot+nod*(p-pivot);n=nod*n;}"                \
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
// Glossy draws tell body paint (1), glazing (2), aluminum (3) and rubber (4)
// apart by the exported colors of those materials in tools/build_assets.py.
// Asphalt (5) is the circuit's road color or one of its darker tire marks;
// lamps and windows that light up at night (6) use the lantern color in biomes.py.
static const char *world_vs[] = {
    GLSL "layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;layout(location=2) in "
         "vec3 aColor;layout(location=3) in float aPaint;\n"
         "uniform mat4 uModel,uVP,uLight;uniform vec3 uPaint,uRoad;out float vGround,vMaterial;"
         "out vec3 vPos,vNormal,vColor;out vec4 vShadow;\n"
         CROWD_GLSL,
    VEGETATION_GLSL
         "void main(){vec3 pos=aPos,normal=aNormal;animateCrowd(pos,normal);animateVegetation(pos,normal);vec4 "
         "p=uModel*vec4(pos,1.);vPos=p.xyz;vNormal=mat3(uModel)*normal;vColor=mix(aColor,uPaint,"
         "clamp(aPaint,0.,1.));vGround=max(0.,-aPaint);"
         "float worn=dot(aColor,uRoad)/dot(uRoad,uRoad);"
         "vMaterial=aPaint>.5?1.:distance(aColor,vec3(.075,.18,.22))<.01?2.:"
         "distance(aColor,vec3(.60,.66,.65))<.01?3.:distance(aColor,vec3(.055,.065,.073))<.01?4.:"
         "distance(aColor,vec3(.98,.80,.40))<.004?6.:"
         "worn>.6&&worn<1.01&&distance(aColor,uRoad*worn)<.004&&aPaint==0.?5.:0.;"
         "vShadow=uLight*p;gl_Position=uVP*p;}"};
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
// The sky dome behind the scene, also seen in distant haze, water and glossy
// paint. The horizon is the former flat fog color; the sun or moon glows in
// the shadow light's direction.
#define SKY_GLSL \
    "uniform vec3 uHorizon,uZenith,uEmber;uniform float uOvercast,uNight;" \
    "vec3 skyColor(vec3 d){d=normalize(d);vec3 sun=normalize(vec3(-.6,1.,.45));" \
    "float s=max(0.,dot(d,sun));vec3 c=mix(uHorizon,uZenith,pow(clamp(d.y,0.,1.),.6));" \
    "c+=uEmber*uNight*1.2*(1.-smoothstep(-.1,.4,d.y));" \
    "vec3 glow=mix(vec3(1.,.84,.58),vec3(.45,.52,.72),uNight)*(1.-.75*uOvercast)*mix(1.,.3,uNight);" \
    "return c+glow*(pow(s,4.)*.12+pow(s,60.)*.4);}" \
    "vec3 fogColor(vec3 d){return skyColor(vec3(d.x,max(d.y,0.)*.4,d.z));}"
static const char *world_fs[] = {GLSL CLOUD_GLSL SKY_GLSL
    "in float vGround,vMaterial;in vec3 vPos,vNormal,vColor;in vec4 vShadow;out vec4 frag;"
    "uniform sampler2DShadow uShadow;uniform "
    "vec3 uEye;uniform float uFogFar,uClipFar,uUnlit,uGloss;uniform bool uBoost;uniform float uBoostTime;\n"
    "uniform vec4 uLamps[8],uLampDirs[8];\n"
    // Night car lamps: a low beam tipped towards the road and a short red spill
    // behind. Softened facing lets the grazing beam still reach the asphalt.
    "vec3 carLamps(vec3 n){vec3 sum=vec3(0.);for(int i=0;i<8;i++){vec4 lamp=uLamps[i];"
    "if(lamp.w<=0.)continue;vec3 f=uLampDirs[i].xyz,d=lamp.xyz-vPos;float r=length(d);"
    "vec3 l=d/max(r,.001);float cone=smoothstep(.83,.95,dot(-l,normalize(f-vec3(0.,.12,0.))));"
    "float facing=clamp(dot(n,l)*2.2+.3,0.,1.);"
    "sum+=vec3(1.,.88,.68)*cone*facing*(1.-smoothstep(22.,36.,r))*1.8/(1.+r*r*.003);"
    "vec3 t=lamp.xyz-f*3.2-vPos;float tr=length(t);"
    "float back=clamp(dot(n,t/max(tr,.001))*.6+.4,0.,1.);"
    "sum+=vec3(.95,.08,.03)*back*(1.-smoothstep(1.5,4.5,tr))*1.2/(1.+tr*tr*.8);}"
    "return sum;}\n"
    "float terrainHash(vec2 p){vec3 q=fract(vec3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);"
    "return fract((q.x+q.y)*q.z);}"
    "float terrainNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);"
    "return mix(mix(terrainHash(i),terrainHash(i+vec2(1,0)),f.x),"
    "mix(terrainHash(i+vec2(0,1)),terrainHash(i+vec2(1,1)),f.x),f.y);}"
    "float shadow(vec3 n){vec3 "
    "p=vShadow.xyz/vShadow.w*.5+.5;if(p.x<0.||p.x>1.||p.y<0.||p.y>1.||p.z>1.)return 1.;float "
    "bias=max(.0005,.0015*(1.-dot(n,normalize(vec3(-.6,1.,.45)))));float s=0.;"
    // Nine bilinear depth comparisons blend into a smooth 4x4-texel penumbra.
    "for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)"
    "s+=texture(uShadow,vec3(p.xy+vec2(x,y)/2048.,p.z-bias));return s/9.;}\n",
    // Ground surfaces, from the paint id the terrain exports: meadow (1), sand
    // (2), snow (3), red desert sand (4), leaf litter (5), jungle floor (6),
    // garden lawn with petals (7), paving slabs (8), heath (9), volcanic ash (10).
    "vec3 groundTone(vec3 base,float kind){float tone=terrainNoise(vPos.xz*.13);"
    "float fine=1.-smoothstep(.12,.65,length(fwidth(vPos.xz)));"
    "float detail=(terrainNoise(vPos.xz*3.5)-.5)*fine;"
    "if(kind<1.5||(kind>6.5&&kind<7.5)){base*=.86+.22*tone+.10*detail;"
    "base=mix(base,base*vec3(1.13,.94,.74),smoothstep(.52,.8,tone)*.4);"
    "if(kind>6.5){float petal=smoothstep(.83,.9,terrainNoise(vPos.xz*2.1+3.))*fine;"
    "base=mix(base,vec3(.97,.74,.82),petal*.6);}return base;}"
    "if(kind>4.5&&kind<5.5){base*=.84+.22*tone+.10*detail;"
    "float leaf=smoothstep(.56,.78,terrainNoise(vPos.xz*1.6+11.));"
    "vec3 fall=mix(vec3(.78,.28,.07),vec3(.93,.62,.15),terrainNoise(vPos.xz*.8+4.));"
    "return mix(base,fall,leaf*.5*(.4+.6*fine));}"
    "if(kind>5.5&&kind<6.5){base*=.80+.26*tone+.12*detail;"
    "return mix(base,base*vec3(.72,.92,.66),smoothstep(.4,.72,terrainNoise(vPos.xz*.35+5.))*.55);}"
    "if(kind>7.5&&kind<8.5){vec2 c=fract(vPos.xz*.25);"
    "float joint=(1.-smoothstep(0.,.022,min(min(c.x,1.-c.x),min(c.y,1.-c.y))))*fine;"
    "return base*(.92+.08*tone+.05*detail)*(1.-.24*joint);}"
    "if(kind>8.5&&kind<9.5){base*=.85+.2*tone+.1*detail;"
    "base=mix(base,vec3(.44,.28,.42),smoothstep(.56,.76,terrainNoise(vPos.xz*.19+2.))*.6);"
    "return mix(base,vec3(.52,.42,.25),smoothstep(.62,.82,terrainNoise(vPos.xz*.33+9.))*.45);}"
    "if(kind>9.5){float grain=(terrainNoise(vPos.xz*6.1)-.5)*fine;"
    "return base*(.78+.34*tone+.12*detail+.14*grain);}"
    "float ripple=sin(vPos.x*.9+vPos.z*.43+tone*5.);"
    "return base*(.93+.09*tone+.04*detail+.018*ripple);}\n",
    "void main(){vec3 n=normalize(vNormal);if(!gl_FrontFacing)n=-n;vec3 "
    "light=normalize(vec3(-.6,1.,.45));float diff=max(0.,dot(n,light));float sh=shadow(n);float "
    // Cool skylight in the shade against a warm sun gives the forms their depth.
    "hemi=n.y*.5+.5;vec3 ambient=mix(vec3(.30,.35,.42),vec3(.58,.65,.74),hemi);"
    "ambient=mix(ambient,mix(vec3(.045,.06,.10),vec3(.12,.16,.26),hemi)+uEmber*mix(1.,.6,hemi),uNight);"
    "vec3 base=vColor,glow=vec3(0.);if(uBoost){vec2 p=vColor.rg;"
    "float phase=(p.y+abs(p.x)*.7-uBoostTime*1.35)/2.;"
    "float stripe=abs(fract(phase)-.5);float aa=max(fwidth(phase),.008);"
    "float arrow=1.-smoothstep(.10-aa,.10+aa,stripe);"
    "float inset=1.-smoothstep(2.20,2.55,abs(p.x));"
    "float ends=smoothstep(0.,.3,p.y)*(1.-smoothstep(5.7,6.,p.y));"
    "base=mix(vec3(.19,.23,.22),vec3(.36,.56,.48),arrow*inset*ends);"
    "glow=vec3(.20,.62,.50)*arrow*inset*ends;"
    "}if(vGround>.5)base=groundTone(base,vGround);"
    // Asphalt: broad worn patches and an aggregate grain that fades out with distance.
    "if(vMaterial>4.5&&vMaterial<5.5){float grain=(terrainNoise(vPos.xz*7.3)-.5)*"
    "(1.-smoothstep(.06,.4,length(fwidth(vPos.xz))));"
    "base*=.93+.13*terrainNoise(vPos.xz*.21+7.)+.10*grain;}float "
    "cloud=cloudShade(vPos);float sun=(1.-.8*uOvercast)*(1.-.82*cloud);"
    "vec3 skyAmbient=ambient*mix(vec3(1.),vec3(.91,.95,1.02),cloud);"
    // The moon shares the sun's direction and shadow map, with a cool, dim light.
    "vec3 sunColor=mix(vec3(.58,.47,.33),vec3(.12,.15,.23),uNight);"
    "vec3 col=base*(skyAmbient*(1.-.05*uOvercast)+sunColor*diff*sh*sun);"
    "if(uNight>0.)col+=(base*carLamps(n)+glow)*uNight;"
    // Lanterns, torches and lit windows (6) glow after dark.
    "if(vMaterial>5.5)col+=base*2.2*uNight;vec3 V=normalize(uEye-vPos),"
    "halfV=normalize(light+V);float "
    "spec=pow(max(0.,dot(n,halfV)),48.)*.12*sh*sun*(1.-.75*uNight);col+=spec;"
    // Clear-coated die-cast paint, glazing and aluminum mirror the sky above and
    // the ground below, with a tight sun glint that the bloom picks up.
    "if(uGloss>0.){float m=vMaterial,coat=uGloss*(m>3.5?.05:m>2.5?.8:m>1.5?1.2:m>.5?.85:.5);"
    "vec3 R=reflect(-V,n),env=R.y>0.?skyColor(R):uHorizon*mix(.5,.22,clamp(-R.y*2.,0.,1.));"
    // A softer Fresnel curve than glass alone: toy lacquer catches the sky early.
    "bool metal=m>2.5&&m<3.5;float f=metal?.6:.06+.7*pow(1.-max(dot(n,V),0.),3.);"
    "col=mix(col,metal?env*base*1.5:env,clamp(f*coat,0.,1.));"
    "col+=pow(max(0.,dot(n,halfV)),160.)*.55*coat*sh*sun*(1.-.75*uNight);}"
    // Fully unlit lamps are brighter than white so that they bloom.
    "col=mix(col,base*(uUnlit>.99?2.4:1.),uUnlit);float dist=length(uEye-vPos),"
    // The haze lies low: hills and peaks rising well above the eye stay clearer.
    // Lit windows and lanterns shine through it at night.
    "fog=smoothstep(uFogFar*.45,uFogFar,dist)*(1.-.5*smoothstep(15.,150.,vPos.y-uEye.y))"
    "*(1.-.45*uNight*step(5.5,vMaterial));"
    "col=mix(col,fogColor(-V),fog*.88);"
    // Distant land dissolves into the sky before the far clip plane cuts it off.
    "col=mix(col,skyColor(-V),smoothstep(uClipFar*.7,uClipFar*.96,dist));frag=vec4(col,1.);}"};
static const char *depth_vs = GLSL "layout(location=0)in vec3 aPos;uniform mat4 uModel,uVP;"
    CROWD_GLSL VEGETATION_GLSL "void main(){vec3 pos=aPos,n=vec3(0.,1.,0.);animateCrowd(pos,n);animateVegetation(pos,n);"
               "gl_Position=uVP*uModel*vec4(pos,1.);}";
static const char *depth_fs = GLSL "void main(){}";
// Instanced camera-facing quads: one draw, no per-drop objects or external assets.
// Positions wrap in world space; moving the chase camera never drags the weather.
static const char *weather_vs = GLSL
    "uniform mat4 uVP;uniform vec3 uCenter,uRight,uUp,uDrift,uWind;"
    "uniform float uTime,uIntensity,uNight;uniform int uKind;uniform vec3 uFlakes,uGustA,uGustB;"
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
    "vec3 tint=rain?vec3(.68,.81,.94):uFlakes;"
    "if(wind)tint=mix(uGustA,uGustB,b);"
    "vTint=vec4(tint*mix(1.,.38,uNight),uIntensity*edge*(rain?.48:wind?.6:.82));gl_Position=uVP*vec4(p,1.);}";
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
static const char *water_fs[] = {GLSL CLOUD_GLSL SKY_GLSL
    "in vec3 vPos,vWater;out vec4 frag;"
    "uniform sampler2D uOpaque,uDepth;uniform sampler2DShadow uShadow;uniform mat4 uVP,uLight;"
    "uniform vec3 uEye;uniform vec2 uResolution,uDepthParams;"
    "uniform vec4 uViewport,uWakes[8];uniform float uTime;"
    "uniform vec3 uWaterTint,uWaterExtinction;uniform float uLavaAbove,uClipFar;"
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
    "float distanceAt(float d){float z=d*2.-1.;return uDepthParams.y/(z+uDepthParams.x);}"
    "vec2 screenUV(vec4 clip){return (clip.xy/clip.w*.5+.5)*uViewport.zw+uViewport.xy;}"
    "vec2 safeUV(vec2 uv){vec2 px=1./uResolution;"
    "return clamp(uv,uViewport.xy+px,uViewport.xy+uViewport.zw-px);}",
    "void main(){vec2 flow=vWater.yz,q=vPos.xz-flow*uTime;"
    "float sunlight=(1.-.85*uOvercast)*(1.-.82*cloudShade(vPos))*(1.-.6*uNight);"
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
    // Lava: a glowing melt under a drifting black crust that cracks open along
    // the flow. It is brighter than white, so the bloom carries its heat.
    "if(vPos.y>uLavaAbove){vec2 lq=vPos.xz-flow*uTime*.35;float n=noise(lq*.28)*.55+noise(lq*1.1+t*.04)*.3+noise(lq*3.7-t*.09)*.15;"
    "float crust=smoothstep(.42,.70,n),vein=1.-smoothstep(.0,.08,abs(n-.42));"
    "vec3 melt=mix(vec3(4.2,1.25,.16),vec3(1.9,.32,.04),smoothstep(.2,.42,n));"
    "vec3 col=mix(melt,vec3(.075,.055,.05),crust*.92)+vec3(2.6,.7,.1)*vein*crust;"
    "col*=1.-.35*shore;float fog=smoothstep(85.,190.,length(uEye-vPos));"
    "col=mix(col,fogColor(-V),fog*.7);frag=vec4(col,smoothstep(0.,.04,depth));return;}"
    "vec2 refrUV=safeUV(uv+grad*.025*min(depth,2.));"
    "if(distanceAt(texture(uDepth,refrUV).r)<waterD)refrUV=uv;"
    "vec3 bed=texture(uOpaque,refrUV).rgb;"
    // Extinction follows the light path through the water, not just vertical
    // depth. Shallows reveal the bed; a pond gains body instead of a milky film.
    "float thickness=min(depth,8.);"
    "vec3 tint=uWaterTint,extinction=uWaterExtinction;"
    "vec3 absorption=exp(-extinction*thickness);"
    "vec3 transmitted=bed*absorption+tint*mix(1.,.22,uNight)*(1.-absorption);"
    "vec2 cq=q+vec2(noise(q*.7+t*.13),noise(q*.6-t*.11))*1.8;"
    "float caustic=pow(max(0.,1.-abs(sin(cq.x*3.1+sin(cq.y*2.7))+sin(cq.y*3.7+t*.4))*.5),18.);"
    "caustic*=1.-smoothstep(.15,.6,length(fwidth(q)));"
    "transmitted+=bed*caustic*.065*exp(-thickness*.8)*sunlight;"
    "vec3 reflected=reflect(-V,n);"
    "vec3 sky=skyColor(vec3(reflected.x,max(reflected.y,0.),reflected.z));"
    "float cloud=cloudCover(vPos.xz+reflected.xz/max(.18,reflected.y)*(110.-vPos.y));"
    "sky=mix(sky,vec3(.92,.94,.90)*mix(1.,.16,uNight),cloud*.32*uCloudAmount);"
    // Short screen-space rays reflect nearby banks, trees and cars; sky fills
    // off-screen rays. Depth comes from the opaque scene, never the water itself.
    // Open water far out in the haze skips the rays.
    "vec3 reflection=sky;float nearby=1.-smoothstep(110.,150.,length(uEye-vPos));"
    "if(nearby>0.){for(int i=1;i<=16;i++){float stepD=float(i)*1.2;"
    "vec3 ray=vPos+n*.18+reflected*stepD;vec4 clip=uVP*vec4(ray,1.);"
    "if(clip.w<=0.)break;vec2 sampleUV=screenUV(clip);"
    "if(any(lessThan(sampleUV,uViewport.xy))||any(greaterThan(sampleUV,uViewport.xy+uViewport.zw)))break;"
    "float sceneD=distanceAt(texture(uDepth,sampleUV).r);"
    "float delta=distanceAt(clip.z/clip.w*.5+.5)-sceneD;"
    "if(delta>0.&&delta<.65){vec2 border=min(sampleUV-uViewport.xy,uViewport.xy+uViewport.zw-sampleUV);"
    "float fade=smoothstep(0.,.045,min(border.x,border.y))*(1.-float(i)/20.)*nearby;"
    "reflection=mix(sky,texture(uOpaque,sampleUV).rgb,fade*.8);break;}}}"
    // Air/water Fresnel reflectance: no constant white varnish at steep views.
    "float fresnel=.0204+.9796*pow(1.-max(0.,dot(n,V)),5.);"
    "vec3 col=mix(transmitted,reflection,fresnel);"
    "vec3 light=normalize(vec3(-.6,1.,.45));vec3 halfV=normalize(light+V);"
    "vec4 sh=uLight*vec4(vPos,1.);vec3 sc=sh.xyz/sh.w*.5+.5;"
    "float lit=1.;if(all(greaterThan(sc,vec3(0)))&&all(lessThan(sc,vec3(1))))"
    "lit=mix(.2,1.,texture(uShadow,vec3(sc.xy,sc.z-.001)));"
    "float glint=pow(max(0.,dot(n,halfV)),300.)*.18+pow(max(0.,dot(n,halfV)),40.)*.04;"
    "col+=mix(vec3(1.,.94,.76),vec3(.62,.70,.88),uNight)*glint*lit*sunlight;"
    "float foam=shore*smoothstep(.68,.9,noise(q*3.))*moving;"
    "col=mix(col,vec3(.75,.83,.79)*mix(1.,.25,uNight),foam*.16);"
    "float dist=length(uEye-vPos),fog=smoothstep(85.,190.,dist);"
    "col=mix(col,fogColor(-V),fog*.88);"
    "col=mix(col,skyColor(-V),smoothstep(uClipFar*.7,uClipFar*.96,dist));"
    "frag=vec4(col,smoothstep(0.,.065,depth));}"};
static const char *spray_vs = GLSL
    "layout(location=0)in vec4 aPosition;layout(location=1)in vec4 aMotion;"
    "uniform mat4 uVP;uniform vec3 uRight,uUp;out vec2 vUV;out float vFade,vKind;"
    "void main(){vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),"
    "vec2(-1,-1),vec2(1,1),vec2(-1,1));vUV=corners[gl_VertexID];"
    "vFade=aMotion.x;vKind=aMotion.y;vec3 p=aPosition.xyz;"
    "float size=aPosition.w;"
    "if(vKind>2.5)p+=(uRight*vUV.x+uUp*vUV.y)*size;"
    "else if(vKind>1.5)p+=vec3(vUV.x,0.,vUV.y)*size;"
    "else p+=uRight*vUV.x*size+uUp*vUV.y*size*(1.3+aMotion.z*.12);"
    "gl_Position=uVP*vec4(p,1.);}";
static const char *spray_fs = GLSL
    "in vec2 vUV;in float vFade,vKind;out vec4 frag;uniform float uNight;"
    "void main(){float d=length(vUV),a;vec3 color;"
    // Lamp halos (3 head, 4 tail) are blended additively over the night scene.
    "if(vKind>2.5){a=pow(max(0.,1.-d),2.2)*vFade;if(a<.008)discard;"
    "frag=vec4(vKind>3.5?vec3(1.,.16,.07):vec3(1.,.90,.70),a);return;}"
    "if(vKind>1.5){a=(1.-smoothstep(.04,.12,abs(d-.78)))*.22;"
    "color=vec3(.76,.88,.85);}else{a=1.-smoothstep(.2,1.,d);"
    "float rim=pow(max(0.,1.-length(vUV-vec2(-.27,.28))*1.5),4.);"
    "color=mix(vec3(.39,.65,.68),vec3(1.),rim+.3);a*=.8;}"
    "a*=vFade;if(a<.008)discard;frag=vec4(color*mix(1.,.35,uNight),a);}";
// The sky is drawn behind the finished opaque scene, at the far plane. Clouds
// sit on the same 110 m layer whose shadows cross the track.
static const char *sky_vs =
    GLSL "out vec2 vNdc;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.-1.;"
         "vNdc=p;gl_Position=vec4(p,1.,1.);}";
static const char *sky_fs = GLSL CLOUD_GLSL SKY_GLSL
    "in vec2 vNdc;out vec4 frag;uniform vec3 uEye,uRight,uUp,uBack;uniform vec4 uProj;"
    "float starHash(vec3 p){p=fract(p*vec3(.1031,.1030,.0973));p+=dot(p,p.yxz+33.33);"
    "return fract((p.x+p.y)*p.z);}"
    "void main(){vec2 v=(vNdc+uProj.zw)/uProj.xy;vec3 d=normalize(uRight*v.x+uUp*v.y-uBack);"
    "vec3 c=skyColor(d);"
    "if(d.y>.02){vec2 p=uEye.xz+d.xz*(110.-uEye.y)/d.y;"
    "float cover=cloudCover(p)*uCloudAmount*smoothstep(.03,.3,d.y);"
    "vec3 lit=mix(vec3(1.,.98,.95),vec3(.10,.12,.17),uNight);"
    "c=mix(c,mix(lit,uHorizon,.3+.4*uOvercast),cover*.7);"
    // Sparse stars: one candidate per cell of a direction grid, a pixel or two wide.
    "if(uNight>0.){vec3 q=d*220.,cell=floor(q);float h=starHash(cell);"
    "vec3 center=cell+.5+(vec3(starHash(cell+7.),starHash(cell+13.),starHash(cell+29.))-.5)*.6;"
    "float size=max(length(fwidth(q))*1.5,.001);"
    "float star=step(.982,h)*max(0.,1.-length(q-center)/size)*(.5+1.3*fract(h*97.));"
    "c+=vec3(.85,.9,1.)*star*(1.-cover)*smoothstep(.06,.3,d.y)*uNight;}}"
    "frag=vec4(c,1.);}";
static const char *post_vs =
    GLSL "out vec2 uv;void main(){vec2 "
         "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}";
// Scalable ambient obscurance (McGuire et al.) at half resolution. A 4x4
// rotation pattern spreads the spiral samples; the blur pass averages it away.
// Distance to the camera is packed beside the result for the blur's depth test.
// Depth is fetched by whole pixels: a half-resolution pixel centre lies exactly
// between four full-resolution ones, and rounding would differ from row to row.
static const char *ao_fs = GLSL QUALITY
    "in vec2 uv;out vec4 frag;uniform sampler2D uDepth;uniform vec4 uProj,uViewport;"
    "uniform vec2 uDepthParams,uSize;uniform float uRadius,uScale,uMaxPixels,uIntensity,uFogFar;"
    "float depthAt(ivec2 c){return texelFetch(uDepth,clamp(c,ivec2(0),ivec2(uSize)-1),0).r;}"
    "vec3 viewPos(ivec2 c){float z=uDepthParams.y/(depthAt(c)*2.-1.+uDepthParams.x);"
    "vec2 ndc=((vec2(c)+.5)/uSize-uViewport.xy)/uViewport.zw*2.-1.;"
    "return vec3((ndc+uProj.zw)/uProj.xy*z,-z);}"
    "int bayer(int x,int y){return((x^y)<<1)|y;}"
    "void main(){ivec2 c=ivec2(gl_FragCoord.xy)*2;vec2 t=(vec2(c)+.5)/uSize;"
    "vec2 lo=uViewport.xy,hi=uViewport.xy+uViewport.zw;"
    "if(depthAt(c)>=1.||any(lessThan(t,lo))||any(greaterThan(t,hi))){frag=vec4(1.);return;}"
    "vec3 p=viewPos(c);"
    "vec3 dx1=viewPos(c+ivec2(1,0))-p,dx2=p-viewPos(c-ivec2(1,0));"
    "vec3 dy1=viewPos(c+ivec2(0,1))-p,dy2=p-viewPos(c-ivec2(0,1));"
    "vec3 n=normalize(cross(abs(dx1.z)<abs(dx2.z)?dx1:dx2,abs(dy1.z)<abs(dy2.z)?dy1:dy2));"
    "ivec2 q=ivec2(gl_FragCoord.xy)&3;"
    "float spin=float(bayer(q.x&1,q.y&1)*4+bayer(q.x>>1,q.y>>1))*.3927;"
    // Offsets are measured in sampling radii, so that close-ups capped at
    // uMaxPixels are not darker than the same shapes seen from afar.
    "float dist=-p.z,radius=min(uRadius*uScale/dist,uMaxPixels),reach=radius*dist/uScale,sum=0.;"
    "for(int i=0;i<AO_SAMPLES;i++){float a=(float(i)+.5)/float(AO_SAMPLES),angle=a*43.98+spin;"
    "vec3 v=(viewPos(c+ivec2(round(vec2(cos(angle),sin(angle))*a*radius)))-p)/reach;"
    "float vv=dot(v,v),vn=dot(v,n),f=max(1.-vv,0.);"
    "sum+=f*f*f*max((vn-.03)/(.01+vv),0.);}"
    "float ao=max(0.,1.-sum*uIntensity*5./float(AO_SAMPLES));"
    "ao=mix(ao,1.,smoothstep(uFogFar*.35,uFogFar*.8,dist));"
    "float key=clamp(dist/320.,0.,1.)*255.;frag=vec4(ao,floor(key)/255.,fract(key),1.);}";
static const char *ao_blur_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uOcclusion;"
    "float distanceAt(vec4 s){return(s.g*255.+s.b)/255.*320.;}"
    "void main(){ivec2 c=ivec2(gl_FragCoord.xy),size=textureSize(uOcclusion,0)-1;"
    "float key=distanceAt(texelFetch(uOcclusion,c,0)),sum=0.,weight=0.;"
    "for(int y=-1;y<=2;y++)for(int x=-1;x<=2;x++){"
    "vec4 s=texelFetch(uOcclusion,clamp(c+ivec2(x,y),ivec2(0),size),0);"
    "float w=max(0.,1.-abs(distanceAt(s)-key)/(.04*key+.1));sum+=s.r*w;weight+=w;}"
    "frag=vec4(vec3(sum/max(weight,1e-4)),1.);}";
// Depth of field for the menu broadcast's lens, at half resolution. Colors are
// weighted by their blur so that sharp subjects never bleed into the background.
// The sharp zone is at least a car's length deep, so a close-up stays crisp nose to tail.
#define COC_GLSL \
    "uniform vec2 uDepthParams;uniform float uFocus;" \
    "float blurAt(float d){float z=uDepthParams.y/(d*2.-1.+uDepthParams.x);" \
    "return clamp((abs(z-uFocus)-max(uFocus*.12,1.8))/z,0.,1.);}"
static const char *dof_down_fs = GLSL COC_GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uScene,uDepth;"
    "void main(){ivec2 c=ivec2(gl_FragCoord.xy)*2,last=textureSize(uScene,0)-1;"
    "vec3 sum=vec3(0.);float weight=0.,blur=0.;"
    "for(int i=0;i<4;i++){ivec2 t=min(c+ivec2(i&1,i>>1),last);float k=blurAt(texelFetch(uDepth,t,0).r);"
    "sum+=texelFetch(uScene,t,0).rgb*(k+.001);weight+=k+.001;blur+=k*.25;}"
    "frag=vec4(sum/weight,blur);}";
// A golden-angle disc; each tap counts only where its own blur reaches the pixel.
static const char *dof_blur_fs = GLSL QUALITY
    "in vec2 uv;out vec4 frag;uniform sampler2D uSource;uniform float uRadius;"
    "void main(){vec2 texel=1./vec2(textureSize(uSource,0));vec4 center=texture(uSource,uv);"
    "float r=center.a*uRadius;vec3 sum=center.rgb*(center.a+.001);float weight=center.a+.001;"
    "for(int i=0;i<DOF_TAPS;i++){float d=sqrt((float(i)+.5)/float(DOF_TAPS))*r,a=float(i)*2.39996;"
    "vec4 s=texture(uSource,uv+vec2(cos(a),sin(a))*d*texel);"
    "float w=clamp(s.a*uRadius-d+1.,0.,1.)*(s.a+.001);sum+=s.rgb*w;weight+=w;}"
    "frag=vec4(sum/weight,center.a);}";
// Dual-filter bloom pyramid. The first level keeps only light above the
// threshold, weighted against single bright pixels so glints do not flicker.
static const char *bloom_down_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uSource;uniform vec2 uTexel;"
    "uniform float uThreshold;uniform bool uPrefilter;"
    "vec4 tap(vec2 p,float k){vec3 c=texture(uSource,p).rgb;if(!uPrefilter)return vec4(c*k,k);"
    "float l=max(c.r,max(c.g,c.b)),soft=clamp(l-uThreshold+.25,0.,.5);"
    "c*=max(soft*soft,l-uThreshold)/max(l,1e-4);k/=1.+max(c.r,max(c.g,c.b));return vec4(c*k,k);}"
    "void main(){vec2 h=uTexel*.5,g=vec2(h.x,-h.y);"
    "vec4 s=tap(uv,4.)+tap(uv-h,1.)+tap(uv+h,1.)+tap(uv+g,1.)+tap(uv-g,1.);frag=vec4(s.rgb/s.a,1.);}";
static const char *bloom_up_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uSource;uniform vec2 uTexel;"
    "vec3 tap(float x,float y){return texture(uSource,uv+vec2(x,y)*uTexel).rgb;}"
    "void main(){frag=vec4((tap(-1.,0.)+tap(1.,0.)+tap(0.,-1.)+tap(0.,1.)+"
    "(tap(-.5,-.5)+tap(.5,-.5)+tap(-.5,.5)+tap(.5,.5))*2.)/12.,1.);}";
// Combines the scene with its occlusion, depth of field and bloom, then grades
// it. The luma for antialiasing travels in alpha.
static const char *post_fs = GLSL COC_GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uScene,uOcclusion,uBloom,uDepth,uDof;uniform vec2 uResolution,"
    "uOcclusionTexel;uniform float uVignette,uSpeedBlur,uFade,uBloomStrength,uAperture;\n"
    // A filmic shoulder rolls lamps, glints and sunlit snow off into white
    // while ordinary surfaces keep their authored colors.
    "vec3 shoulder(vec3 c){vec3 k=vec3(.84);return mix(c,k+(1.-k)*(1.-exp(-(c-k)/(1.-k))),step(k,c));}"
    "int bayer(int x,int y){return((x^y)<<1)|y;}"
    "void main(){vec2 px=1./uResolution;vec3 c=texture(uScene,uv).rgb;"
    // The blurred occlusion stands a quarter texel off its centre (sampled at the
    // even full pixel, averaged over texels -1..2).
    "c*=texture(uOcclusion,uv-.25*uOcclusionTexel).r;vec2 q=uv*(1.-uv);"
    "if(uAperture>0.){float k=blurAt(texture(uDepth,uv).r)*uAperture*uResolution.y;"
    "c=mix(c,texture(uDof,uv).rgb,smoothstep(.5,2.5,k));}"
    // A radial trail only at the sides; the road ahead and HUD stay sharp.
    "float blur=uSpeedBlur*smoothstep(.22,.49,abs(uv.x-.5));"
    "if(blur>.0001){vec2 ray=uv-vec2(.5,.58);vec3 trail=c;"
    "for(int i=1;i<=8;i++){vec2 tap=uv-ray*(blur*float(i)/8.);"
    "trail+=texture(uScene,clamp(tap,px*.5,1.-px*.5)).rgb;}"
    "c=mix(c,trail/9.,.85);}"
    "c=shoulder(c+texture(uBloom,uv).rgb*uBloomStrength);"
    // Gentle contrast, vibrance for muted colors, cool shadows and warm highlights.
    "float l=dot(c,vec3(.2126,.7152,.0722)),sat=max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));"
    "c=max(mix(vec3(l),c,1.+.15*(1.-sat)),0.);c=mix(c,c*c*(3.-2.*c),.14);"
    "c*=mix(vec3(.985,1.,1.02),vec3(1.02,1.,.98),smoothstep(.08,.7,l));float "
    "vign=clamp(pow(16.*q.x*q.y,.22),.65,1.);c*=mix(1.,vign,uVignette)*(1.-uFade);"
    // Ordered dither breaks up banding in the sky and haze gradients.
    "ivec2 d=ivec2(gl_FragCoord.xy)&3;"
    "c=clamp(c,0.,1.)+(float(bayer(d.x&1,d.y&1)*4+bayer(d.x>>1,d.y>>1))/16.-.47)/255.;"
    "frag=vec4(c,dot(c,vec3(.299,.587,.114)));}";
// FXAA 3.11 (Lottes), quality preset with a 12.5-pixel edge search.
static const char *fxaa_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uImage;uniform vec2 uResolution;"
    "float luma(vec2 p){return texture(uImage,p).a;}"
    "void main(){vec2 px=1./uResolution;vec4 rgbyM=texture(uImage,uv);float lumaM=rgbyM.a;"
    "float lumaS=luma(uv-vec2(0.,px.y)),lumaE=luma(uv+vec2(px.x,0.)),"
    "lumaN=luma(uv+vec2(0.,px.y)),lumaW=luma(uv-vec2(px.x,0.));"
    "float maxL=max(max(lumaN,lumaS),max(lumaE,max(lumaW,lumaM))),"
    "minL=min(min(lumaN,lumaS),min(lumaE,min(lumaW,lumaM))),range=maxL-minL;"
    "if(range<max(.0312,maxL*.125)){frag=vec4(rgbyM.rgb,1.);return;}"
    "float lumaNW=luma(uv+vec2(-px.x,px.y)),lumaSE=luma(uv+vec2(px.x,-px.y)),"
    "lumaNE=luma(uv+px),lumaSW=luma(uv-px);"
    "float lumaNS=lumaN+lumaS,lumaWE=lumaW+lumaE,lumaNESE=lumaNE+lumaSE,lumaNWNE=lumaNW+lumaNE,"
    "lumaNWSW=lumaNW+lumaSW,lumaSWSE=lumaSW+lumaSE;"
    "float edgeHorz=abs(-2.*lumaW+lumaNWSW)+abs(-2.*lumaM+lumaNS)*2.+abs(-2.*lumaE+lumaNESE);"
    "float edgeVert=abs(-2.*lumaS+lumaSWSE)+abs(-2.*lumaM+lumaWE)*2.+abs(-2.*lumaN+lumaNWNE);"
    "bool horzSpan=edgeHorz>=edgeVert;float lengthSign=horzSpan?px.y:px.x;"
    "float subpixB=((lumaNS+lumaWE)*2.+lumaNWSW+lumaNESE)/12.-lumaM;"
    "if(!horzSpan){lumaN=lumaW;lumaS=lumaE;}"
    "float gradientN=lumaN-lumaM,gradientS=lumaS-lumaM,lumaNN=lumaN+lumaM,lumaSS=lumaS+lumaM;"
    "bool pairN=abs(gradientN)>=abs(gradientS);float gradient=max(abs(gradientN),abs(gradientS));"
    "if(pairN)lengthSign=-lengthSign;else lumaNN=lumaSS;"
    "float subpixC=clamp(abs(subpixB)/range,0.,1.);"
    "vec2 posB=uv,offNP=horzSpan?vec2(px.x,0.):vec2(0.,px.y);"
    "if(horzSpan)posB.y+=lengthSign*.5;else posB.x+=lengthSign*.5;"
    "vec2 posN=posB-offNP,posP=posB+offNP;"
    "float gradientScaled=gradient*.25,lumaMM=lumaM-lumaNN*.5;bool lumaMLTZero=lumaMM<0.;"
    "float lumaEndN=luma(posN)-lumaNN*.5,lumaEndP=luma(posP)-lumaNN*.5;"
    "bool doneN=abs(lumaEndN)>=gradientScaled,doneP=abs(lumaEndP)>=gradientScaled;"
    "const float steps[5]=float[5](1.5,2.,2.,2.,4.);"
    "for(int i=0;i<5;i++){if(!doneN)posN-=offNP*steps[i];if(!doneP)posP+=offNP*steps[i];"
    "if(doneN&&doneP)break;"
    "if(!doneN){lumaEndN=luma(posN)-lumaNN*.5;doneN=abs(lumaEndN)>=gradientScaled;}"
    "if(!doneP){lumaEndP=luma(posP)-lumaNN*.5;doneP=abs(lumaEndP)>=gradientScaled;}}"
    "float dstN=horzSpan?uv.x-posN.x:uv.y-posN.y,dstP=horzSpan?posP.x-uv.x:posP.y-uv.y;"
    "bool directionN=dstN<dstP;"
    "bool goodSpan=directionN?(lumaEndN<0.)!=lumaMLTZero:(lumaEndP<0.)!=lumaMLTZero;"
    "float pixelOffset=goodSpan?.5-min(dstN,dstP)/(dstP+dstN):0.;"
    "float subpixF=(-2.*subpixC+3.)*subpixC*subpixC;"
    "float offset=max(pixelOffset,subpixF*subpixF*.75);vec2 p=uv;"
    "if(horzSpan)p.y+=offset*lengthSign;else p.x+=offset*lengthSign;"
    "frag=vec4(texture(uImage,p).rgb,1.);}";

static GLuint render_program_parts(const char *const *vs, int vs_count, const char *const *fs,
                                   int fs_count) {
    GLuint shaders[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    char log[4096];
    for (int i = 0; i < 2; i++) {
        glShaderSource(shaders[i], i ? fs_count : vs_count, i ? fs : vs, NULL);
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
static GLuint render_program_sources(const char *vs, const char *const *fs, int fs_count) {
    return render_program_parts(&vs, 1, fs, fs_count);
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
// Reads and validates a TCM1, TCA1, TCF1 or TCV1 file. Returns the file (to be
// released with SDL_free) and its vertex data; *floats is 10 or 18 per vertex.
static unsigned char *mesh_read(const char *path, uint32_t *vertex_count, size_t *floats_out,
                                bool *animated, bool *wildlife_out) {
    size_t size;
    unsigned char *d = asset_read(path, &size);
    if (!d || size < 8 || (memcmp(d, "TCM1", 4) && memcmp(d, "TCA1", 4) && memcmp(d, "TCF1", 4) && memcmp(d, "TCV1", 4))) {
        SDL_Log("Cannot load mesh %s", path);
        SDL_free(d);
        return NULL;
    }
    uint32_t count;
    bool wildlife = !memcmp(d, "TCF1", 4);
    bool vegetation = !memcmp(d, "TCV1", 4);
    bool crowd = !memcmp(d, "TCA1", 4) || wildlife || vegetation;
    size_t floats = crowd ? 18 : 10;
    memcpy(&count, d + 4, 4);
    count = SDL_Swap32LE(count);
    if (count > 8000000 || count % 3 || size != 8 + (size_t)count * floats * sizeof(float)) {
        SDL_free(d);
        return NULL;
    }
    unsigned char *data = d + 8;
    // The format is explicitly little endian, including IEEE754 components.
    for (size_t i = 0; i < (size_t)count * floats; i++) {
        uint32_t bits;
        memcpy(&bits, data + i * 4, 4);
        bits = SDL_Swap32LE(bits);
        float f;
        memcpy(&f, &bits, 4);
        if (!isfinite(f)) {
            SDL_free(d);
            return NULL;
        }
        memcpy(data + i * 4, &bits, 4);
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
                    return NULL;
                }
                continue;
            }
            if (v[14] < 0 || v[14] > 4 || floorf(v[14]) != v[14] || v[15] < 0 ||
                v[15] > 3 || floorf(v[15]) != v[15] ||
                (wildlife ? (fabsf(v[16]) > .21f || fabsf(v[17]) > .21f || v[13] < -.8f || v[13] > 1.5f) :
                 (v[16] < .5f || v[16] > 2 || v[17] < .5f || v[17] > 2))) {
                SDL_Log("Invalid animation data: %s", path);
                SDL_free(d);
                return NULL;
            }
        }
    }
    *vertex_count = count;
    *floats_out = floats;
    *animated = crowd;
    if (wildlife_out)
        *wildlife_out = wildlife;
    return d;
}
static bool mesh_load(Mesh *m, const char *path) {
    uint32_t count;
    size_t floats;
    bool crowd;
    unsigned char *d = mesh_read(path, &count, &floats, &crowd, NULL);
    if (!d)
        return false;
    *m = mesh_upload_layout(d + 8, (int)count, crowd);
    SDL_free(d);
    return true;
}
#define TILE_SIZE 40.f
#define TILE_GRID 13 // Tiles across the widest island, about 520 m
#define LAND_TILE 125.f // The land beyond the island, out to about 810 m
// Sorts whole triangles into ground tiles by their centre and records each
// tile's bounds. Animated vertices sway, walk or fly up to reach metres.
// With top, also records the highest point over each LAND_CELL cell.
static bool tiled_load(TiledMesh *t, const char *path, float tile_size, float *top) {
    uint32_t count;
    size_t floats;
    bool animated, wildlife;
    unsigned char *d = mesh_read(path, &count, &floats, &animated, &wildlife);
    if (!d)
        return false;
    const float *v = (const float *)(const void *)(d + 8);
    size_t stride = floats * sizeof(float), triangle = stride * 3;
    uint32_t triangles = count / 3;
    int *tile_of = SDL_malloc(sizeof(int) * (triangles ? triangles : 1));
    int counts[TILE_GRID * TILE_GRID] = {0};
    unsigned char *sorted = SDL_malloc(triangle * (triangles ? triangles : 1));
    if (!tile_of || !sorted) {
        SDL_free(tile_of);
        SDL_free(sorted);
        SDL_free(d);
        return false;
    }
    float origin = -tile_size * TILE_GRID * .5f;
    for (int k = 0; top && k < LAND_GRID * LAND_GRID; k++)
        top[k] = -1000;
    for (uint32_t i = 0; i < triangles; i++) {
        const float *a = v + i * 3 * floats, *b = a + floats, *c = b + floats;
        float x = (a[0] + b[0] + c[0]) / 3, z = (a[2] + b[2] + c[2]) / 3;
        int tx = (int)clampf(floorf((x - origin) / tile_size), 0, TILE_GRID - 1);
        int tz = (int)clampf(floorf((z - origin) / tile_size), 0, TILE_GRID - 1);
        tile_of[i] = tz * TILE_GRID + tx;
        counts[tile_of[i]]++;
        if (!top)
            continue;
        // Every cell the triangle's bounds touch, so large distant triangles count too.
        float high = fmaxf(a[1], fmaxf(b[1], c[1]));
        int x0 = (int)floorf(fminf(a[0], fminf(b[0], c[0])) / LAND_CELL) + LAND_GRID / 2;
        int x1 = (int)floorf(fmaxf(a[0], fmaxf(b[0], c[0])) / LAND_CELL) + LAND_GRID / 2;
        int z0 = (int)floorf(fminf(a[2], fminf(b[2], c[2])) / LAND_CELL) + LAND_GRID / 2;
        int z1 = (int)floorf(fmaxf(a[2], fmaxf(b[2], c[2])) / LAND_CELL) + LAND_GRID / 2;
        for (int cz = SDL_max(z0, 0); cz <= SDL_min(z1, LAND_GRID - 1); cz++)
            for (int cx = SDL_max(x0, 0); cx <= SDL_min(x1, LAND_GRID - 1); cx++)
                top[cz * LAND_GRID + cx] = fmaxf(top[cz * LAND_GRID + cx], high);
    }
    int start[TILE_GRID * TILE_GRID], used = 0;
    for (int k = 0; k < TILE_GRID * TILE_GRID; k++) {
        start[k] = used;
        used += counts[k];
    }
    int cursor[TILE_GRID * TILE_GRID];
    memcpy(cursor, start, sizeof cursor);
    for (uint32_t i = 0; i < triangles; i++)
        memcpy(sorted + (size_t)cursor[tile_of[i]]++ * triangle, (const unsigned char *)v + i * triangle,
               triangle);
    int filled = 0;
    for (int k = 0; k < TILE_GRID * TILE_GRID; k++)
        filled += counts[k] > 0;
    t->tiles = SDL_calloc(filled ? (size_t)filled : 1, sizeof *t->tiles);
    t->tile_count = 0;
    // Birds circle 12 m about their origin; ground animals and trees move less.
    float reach = wildlife ? 13.5f : animated ? 2.f : .1f;
    for (int k = 0; k < TILE_GRID * TILE_GRID && t->tiles; k++) {
        if (!counts[k])
            continue;
        MeshTile *tile = &t->tiles[t->tile_count++];
        tile->first = start[k] * 3;
        tile->count = counts[k] * 3;
        for (int j = 0; j < 3; j++) {
            tile->box[j] = 1e9f;
            tile->box[j + 3] = -1e9f;
        }
        const float *p = (const float *)(const void *)(sorted + (size_t)start[k] * triangle);
        for (int n = 0; n < counts[k] * 3; n++, p += floats)
            for (int j = 0; j < 3; j++) {
                tile->box[j] = fminf(tile->box[j], p[j] - reach);
                tile->box[j + 3] = fmaxf(tile->box[j + 3], p[j] + reach);
            }
    }
    t->mesh = mesh_upload_layout(sorted, (int)count, animated);
    SDL_free(tile_of);
    SDL_free(sorted);
    SDL_free(d);
    return t->tiles != NULL;
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
// Planes a x + b y + c z + d >= 0 of a view-projection frustum.
static void set_frustum(Renderer *r, Mat4 vp) {
    const float *m = vp.m;
    for (int i = 0; i < 6; i++) {
        int axis = i / 2;
        float sign = i % 2 ? -1 : 1;
        float len = 0;
        for (int j = 0; j < 4; j++) {
            r->frustum[i][j] = m[j * 4 + 3] + sign * m[j * 4 + axis];
            if (j < 3)
                len += r->frustum[i][j] * r->frustum[i][j];
        }
        len = sqrtf(fmaxf(len, 1e-12f));
        for (int j = 0; j < 4; j++)
            r->frustum[i][j] /= len;
    }
}
static bool box_visible(const Renderer *r, const float *box) {
    for (int i = 0; i < 6; i++) {
        const float *f = r->frustum[i];
        float x = f[0] > 0 ? box[3] : box[0], y = f[1] > 0 ? box[4] : box[1], z = f[2] > 0 ? box[5] : box[2];
        if (f[0] * x + f[1] * y + f[2] * z + f[3] < 0)
            return false;
    }
    return true;
}
// Draws the tiles inside the current frustum, joining neighbouring ranges.
static void draw_tiled(Renderer *r, const TiledMesh *t, Color paint, bool depth) {
    if (!t->mesh.vao)
        return;
    GLuint p = depth ? r->depth_shader : r->shader;
    uniform_matrix(p, "uModel", identity());
    if (!depth)
        glUniform3f(glGetUniformLocation(p, "uPaint"), paint.r, paint.g, paint.b);
    if (r->bound_vao != t->mesh.vao) {
        glBindVertexArray(t->mesh.vao);
        r->bound_vao = t->mesh.vao;
    }
    int first = -1, count = 0;
    for (int i = 0; i <= t->tile_count; i++) {
        const MeshTile *tile = i < t->tile_count ? &t->tiles[i] : NULL;
        bool visible = tile && box_visible(r, tile->box);
        if (visible && first >= 0 && first + count == tile->first) {
            count += tile->count;
            continue;
        }
        if (count) {
            glDrawArrays(GL_TRIANGLES, first, count);
            r->draw_calls++;
            r->triangles += count / 3;
        }
        first = visible ? tile->first : -1;
        count = visible ? tile->count : 0;
    }
}
// Reflective finish for the vehicles that follow; zero restores matte scenery.
static void set_gloss(const Renderer *r, bool depth, float amount) {
    if (!depth)
        glUniform1f(glGetUniformLocation(r->shader, "uGloss"), amount);
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
    // Night lenses sit just proud of each car's modeled headlamps and tail lamps.
    for (int model = 0; model < CAR_MODEL_COUNT; model++) {
        Vertex lamps[4 * 36];
        for (int i = 0; i < 4; i++) {
            bool tail = i >= 2;
            Vec3 center = car_lamp(model, i), half = CAR_LAMPS[model].half[tail];
            Color color = tail ? rgb(1, .10f, .05f) : rgb(1, .96f, .84f);
            for (int k = 0; k < 36; k++) {
                Vertex v = cube[k];
                v.p[0] = center.x + v.p[0] * half.x;
                v.p[1] = center.y + v.p[1] * half.y;
                v.p[2] = center.z + v.p[2] * half.z;
                v.c[0] = color.r;
                v.c[1] = color.g;
                v.c[2] = color.b;
                v.paint = 0;
                lamps[i * 36 + k] = v;
            }
        }
        r->lamps[model] = mesh_upload(lamps, 4 * 36);
    }
    r->cone = prop_mesh(false);
    r->tire = prop_mesh(true);
    r->fence = mesh_upload(NULL,0);
}

static void target_storage(GLuint texture, GLenum format, int w, int h) {
    bool depth = format == GL_DEPTH_COMPONENT24;
    GLenum type = depth ? GL_UNSIGNED_INT : format == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT;
    GLenum layout = depth ? GL_DEPTH_COMPONENT : format == GL_R11F_G11F_B10F ? GL_RGB : GL_RGBA;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, layout, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depth ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depth ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static void mesh_delete(Mesh *m);
static void tiled_delete(TiledMesh *t) {
    mesh_delete(&t->mesh);
    SDL_free(t->tiles);
    *t = (TiledMesh){0};
}
static void scene_delete(TrackScene *s) {
    tiled_delete(&s->scene);
    tiled_delete(&s->vegetation);
    tiled_delete(&s->crowd);
    tiled_delete(&s->wildlife);
    tiled_delete(&s->land);
    mesh_delete(&s->water);
    s->water = (Mesh){0};
    s->track = -1;
}
bool renderer_load_track(Renderer *r, int track) {
    if (r->scene.track == track)
        return true;
    Uint64 start = SDL_GetTicksNS();
    scene_delete(&r->scene);
    const char *id = TRACK_INFO[track].id;
    char path[6][96];
    const char *suffix[6] = {".tcm", "-vegetation.tcv", "-crowd.tca", "-wildlife.tcf", "-water.tcm", "-land.tcm"};
    for (int i = 0; i < 6; i++)
        SDL_snprintf(path[i], sizeof path[i], "tracks/%s%s", id, suffix[i]);
    bool ok = tiled_load(&r->scene.scene, path[0], TILE_SIZE, NULL) &&
              tiled_load(&r->scene.vegetation, path[1], TILE_SIZE, NULL) &&
              tiled_load(&r->scene.crowd, path[2], TILE_SIZE, NULL) &&
              tiled_load(&r->scene.wildlife, path[3], TILE_SIZE, NULL) &&
              mesh_load(&r->scene.water, path[4]) &&
              tiled_load(&r->scene.land, path[5], LAND_TILE, r->scene.land_top);
    r->bound_vao = 0;
    if (!ok) {
        SDL_Log("Cannot load the scene of %s", id);
        scene_delete(&r->scene);
        return false;
    }
    r->scene.track = track;
    SDL_Log("Loaded %s in %.0f ms (%d scene tiles)", id, (SDL_GetTicksNS() - start) / 1e6,
            r->scene.scene.tile_count);
    return true;
}
bool renderer_init(Renderer *r) {
    memset(r, 0, sizeof *r);
    r->scene.track = -1;
    r->last_track = -1;
    r->hero_frame = .30f;
    r->shader = render_program_parts(world_vs, SDL_arraysize(world_vs), world_fs, SDL_arraysize(world_fs));
    r->depth_shader = render_program(depth_vs, depth_fs);
    r->post_shader = render_program(post_vs, post_fs);
    r->weather_shader = render_program(weather_vs, weather_fs);
    r->water_shader = render_program_sources(water_vs, water_fs, SDL_arraysize(water_fs));
    r->spray_shader = render_program(spray_vs, spray_fs);
    r->sky_shader = render_program(sky_vs, sky_fs);
    r->ao_shader = render_program(post_vs, ao_fs);
    r->ao_blur_shader = render_program(post_vs, ao_blur_fs);
    r->bloom_down_shader = render_program(post_vs, bloom_down_fs);
    r->bloom_up_shader = render_program(post_vs, bloom_up_fs);
    r->fxaa_shader = render_program(post_vs, fxaa_fs);
    r->dof_down_shader = render_program(post_vs, dof_down_fs);
    r->dof_blur_shader = render_program(post_vs, dof_blur_fs);
    if (!r->shader || !r->depth_shader || !r->post_shader || !r->weather_shader ||
        !r->water_shader || !r->spray_shader || !r->sky_shader || !r->ao_shader ||
        !r->ao_blur_shader || !r->bloom_down_shader || !r->bloom_up_shader || !r->fxaa_shader ||
        !r->dof_down_shader || !r->dof_blur_shader)
        return false;
    const char *car_models[CAR_MODEL_COUNT] = {"models/car.tcm", "models/sprite.tcm",
                                                "models/bruiser.tcm", "models/comet.tcm"};
    for (int i = 0; i < CAR_MODEL_COUNT; i++)
        if (!mesh_load(&r->cars[i], car_models[i]))
            return false;
    if (!mesh_load(&r->fuel, "models/fuel.tcm"))
        return false;
    if (!mesh_load(&r->helicopter, "models/helicopter.tcm") ||
        !mesh_load(&r->helicopter_rotor, "models/helicopter-rotor.tcm") ||
        !mesh_load(&r->helicopter_tail_rotor, "models/helicopter-tail-rotor.tcm"))
        return false;
    const char *rail_models[RAIL_MODEL_COUNT] = {
        "models/steam-locomotive.tcm", "models/steam-tender.tcm", "models/coach.tcm",
        "models/alpine-locomotive.tcm", "models/alpine-coach.tcm",
        "models/road-train-tractor.tcm", "models/road-train-carriage.tcm",
        "models/diesel-locomotive.tcm", "models/container-wagon.tcm"};
    for (int i = 0; i < RAIL_MODEL_COUNT; i++)
        if (!mesh_load(&r->rail_models[i], rail_models[i])) return false;
    glGenVertexArrays(1, &r->spray_vao);
    glBindVertexArray(r->spray_vao);
    glGenBuffers(1, &r->spray_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->spray_vbo);
    // Spray particles, followed by four night lamp halos per car and the railway's lamps.
    glBufferData(GL_ARRAY_BUFFER, (MAX_PARTICLES + CAR_COUNT * 4 + RAIL_HALOS) * 8 * sizeof(float),
                 NULL, GL_STREAM_DRAW);
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
    // Hardware comparison with bilinear filtering: each shadow tap is already soft.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
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
    glGenTextures(1, &r->scene_depth);
    glGenFramebuffers(1, &r->opaque_fbo);
    glGenTextures(1, &r->opaque_texture);
    glGenTextures(1, &r->opaque_depth);
    glGenFramebuffers(2, r->ao_fbo);
    glGenTextures(2, r->ao_texture);
    glGenFramebuffers(BLOOM_LEVELS, r->bloom_fbo);
    glGenTextures(BLOOM_LEVELS, r->bloom_texture);
    glGenFramebuffers(1, &r->ldr_fbo);
    glGenTextures(1, &r->ldr_texture);
    glGenFramebuffers(2, r->dof_fbo);
    glGenTextures(2, r->dof_texture);
    // OpenGL ES 3.0 renders to floating point only with an extension; probe for
    // the packed 32-bit format first, then half floats, else stay at 8 bits.
    const GLenum formats[] = {GL_R11F_G11F_B10F, GL_RGBA16F};
    r->scene_format = GL_RGBA8;
    for (int f = 0; f < 2 && r->scene_format == GL_RGBA8; f++) {
        for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
        }
        GLuint probe;
        glGenTextures(1, &probe);
        target_storage(probe, formats[f], 4, 4);
        glBindFramebuffer(GL_FRAMEBUFFER, r->scene_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probe, 0);
        if (glGetError() == GL_NO_ERROR &&
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            r->scene_format = formats[f];
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &probe);
    }
    if (r->scene_format == GL_RGBA8)
        SDL_Log("Floating-point rendering unavailable; using an 8-bit scene");
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
    return true;
}
static void target_attach(GLuint fbo, GLuint color, GLuint depth, const char *name) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        SDL_Log("%s framebuffer incomplete", name);
}
static int bloom_size(int size, int level) {
    return SDL_max(1, size >> (level + 1));
}
void renderer_resize(Renderer *r, int w, int h) {
    if (w == r->width && h == r->height)
        return;
    r->width = w;
    r->height = h;
    // Depth of field keeps its blur amount in alpha, so it needs a four-channel format.
    GLenum color = r->scene_format, blurred = color == GL_RGBA8 ? GL_RGBA8 : GL_RGBA16F;
    target_storage(r->scene_texture, color, w, h);
    target_storage(r->scene_depth, GL_DEPTH_COMPONENT24, w, h);
    target_attach(r->scene_fbo, r->scene_texture, r->scene_depth, "Scene");
    target_storage(r->opaque_texture, color, w, h);
    target_storage(r->opaque_depth, GL_DEPTH_COMPONENT24, w, h);
    target_attach(r->opaque_fbo, r->opaque_texture, r->opaque_depth, "Water refraction");
    for (int i = 0; i < 2; i++) {
        target_storage(r->ao_texture[i], GL_RGBA8, (w + 1) / 2, (h + 1) / 2);
        target_attach(r->ao_fbo[i], r->ao_texture[i], 0, "Occlusion");
        target_storage(r->dof_texture[i], blurred, (w + 1) / 2, (h + 1) / 2);
        target_attach(r->dof_fbo[i], r->dof_texture[i], 0, "Depth of field");
    }
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        target_storage(r->bloom_texture[i], color, bloom_size(w, i), bloom_size(h, i));
        target_attach(r->bloom_fbo[i], r->bloom_texture[i], 0, "Bloom");
    }
    target_storage(r->ldr_texture, GL_RGBA8, w, h);
    target_attach(r->ldr_fbo, r->ldr_texture, 0, "Antialiasing");
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
}

// The highest land around a point, blended between cell centres so that a
// camera held above it glides instead of stepping; far below off the land.
static float land_top(const Renderer *r, Vec3 p) {
    float fx = p.x / LAND_CELL + LAND_GRID / 2 - .5f, fz = p.z / LAND_CELL + LAND_GRID / 2 - .5f;
    int x = (int)floorf(fx), z = (int)floorf(fz);
    float h[4];
    for (int k = 0; k < 4; k++) {
        int cx = x + (k & 1), cz = z + (k >> 1);
        bool inside = cx >= 0 && cz >= 0 && cx < LAND_GRID && cz < LAND_GRID;
        h[k] = inside ? r->scene.land_top[cz * LAND_GRID + cx] : -1000;
    }
    return lerpf(lerpf(h[0], h[1], fx - x), lerpf(h[2], h[3], fx - x), fz - z);
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
    // Its patrol can leave the island; it clears the hills and trees beyond.
    p.y = fmaxf(p.y, land_top(r, p) + 14);
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
    // Anti-collision beacon on the tail fin; it blinks on simulation time.
    if (!depth && r->night > 0 && fmodf(time, 1.3f) < .16f) {
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 1);
        Mat4 beacon = mmul(model, mmul(translate(v3(0, 1.78f, -4.15f)), scale3(.1f, .1f, .1f)));
        draw_mesh(r, r->cube, beacon, rgb(1, .12f, .06f), false);
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    }
}

static Vec3 transform_point(Mat4 m, Vec3 p) {
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

// The menu pack races in a tight bunch, the player's car chasing at its tail
// so the chase camera never meets a car from behind.
#define HERO_SPEED BROADCAST_SPEED
#define HERO_SHOT 6.5f
#define HERO_GAP 7.5f
static int hero_slot(int car) {
    return (car + CAR_COUNT - 1) % CAR_COUNT;
}
static float hero_distance(const Game *g, int car) {
    int slot = hero_slot(car);
    float surge = sinf(g->clock * .45f + slot * 2.1f) * 2.2f;
    return g->clock * HERO_SPEED + BROADCAST_START - slot * HERO_GAP + surge;
}
static float hero_lane(const Game *g, int car) {
    int slot = hero_slot(car);
    return (slot % 2 ? 1.7f : -1.7f) + sinf(g->clock * .31f + slot * 1.3f) * .35f;
}

// A ramp's lip launches the pack the way update_car would at HERO_SPEED, rather
// than dropping it back onto the road in one frame. The attitude eases from the
// deck's slope to the ground below, so the car touches down on its wheels.
static void hero_jump(const Track *t, float distance, float lane, Vec3 *pos, float *pitch,
                      float *roll) {
    float climb = (HERO_SPEED * .265f + 1.3f) / HERO_SPEED, fall = 10 / (HERO_SPEED * HERO_SPEED);
    for (int j = 0; j < t->ramp_count; j++) {
        const Ramp *ramp = &t->ramps[j];
        const TrackPoint *rp = &t->points[(int)ramp->index];
        float past = fmodf(distance - rp->s - ramp->length * .5f, t->length);
        if (past < 0)
            past += t->length;
        // Start just short of the lip, where the deck still holds the car higher.
        if (past > t->length - 1)
            past -= t->length;
        if (past > 40 || fabsf(lane) >= ramp->half)
            continue;
        float lip = rp->p.y + ramp->height, y = lip + past * (climb - fall * past);
        if (y <= pos->y)
            return;
        float drop = fmaxf(0, lip - pos->y);
        float landing = (climb + sqrtf(climb * climb + 4 * fall * drop)) / (2 * fall);
        float f = clampf(past / landing, 0, 1);
        f = f * f * (3 - 2 * f);
        *pitch = lerpf(-atanf(ramp->height / ramp->length), *pitch, f);
        *roll *= f;
        pos->y = y;
        return;
    }
}

// Interpolated once per frame so both passes and the night lamps agree.
static void update_cars(Renderer *r, const Game *g, bool menu) {
    const Track *t = &g->tracks[g->selected];
    for (int i = 0; i < CAR_COUNT; i++) {
        Vec3 pos;
        float yaw, pitch = 0, roll = 0;
        r->car_visible[i] = true;
        if (menu) {
            Vec3 d;
            float distance = hero_distance(g, i), lane = hero_lane(g, i);
            pos = track_at(t, distance, lane, &d);
            yaw = atan2f(d.x, d.z);
            car_surface_pose(t, &pos, yaw, &pitch, &roll);
            hero_jump(t, distance, lane, &pos, &pitch, &roll);
            // Rivals cycle through the other liveries so the player's car stays unique.
            int color = g->profile.color;
            r->car_colors[i] = CAR_COLORS[i ? (color + 1 + (i - 1) % 5) % 6 : color];
            r->car_meshes[i] = i ? rival_model(i) : g->profile.car;
        } else {
            const Car *c = &g->cars[i];
            // Use simulation time so blinking freezes with the game, including shadows.
            if (c->reset_protection > 0 && ((int)(c->reset_protection * 8) % 2) != 0) {
                r->car_visible[i] = false;
                continue;
            }
            pos = vlerp(c->prev, c->pos, r->alpha);
            yaw = c->prev_yaw + angle_delta(c->yaw, c->prev_yaw) * r->alpha;
            pitch = c->pitch;
            roll = c->roll;
            r->car_colors[i] = c->color;
            r->car_meshes[i] = c->model;
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
        r->car_models[i] = model;
    }
}

/* Railway: the train's vehicles, barrier arms and flashing signals. Lamps are
   unlit boxes; at night draw_lamp_glows adds their halos. */
// Headlamps of each leading vehicle in its own coordinates.
static const Vec3 RAIL_HEADLAMPS[RAIL_MODEL_COUNT][3] = {
    [RAIL_MODEL_STEAM_LOCOMOTIVE] = {{0, 3.0f, 3.52f}, {-.7f, 1.55f, 3.7f}, {.7f, 1.55f, 3.7f}},
    [RAIL_MODEL_ALPINE_LOCOMOTIVE] = {{-.72f, 1.68f, 4.9f}, {.72f, 1.68f, 4.9f}, {0, 3.5f, 4.82f}},
    [RAIL_MODEL_ROAD_TRAIN_TRACTOR] = {{0, 1.95f, 2.22f}, {0, 1.95f, 2.22f}, {0, 1.95f, 2.22f}},
};
static Mat4 rail_vehicle_model(const RailVehicle *v) {
    return mmul(translate(v->pos), mmul(rotate_y(atan2f(v->forward.x, v->forward.z)),
                                        rotate_x(v->pitch)));
}
static Mat4 rail_arm_model(const RailArm *a, float raise) {
    return mmul(translate(a->pivot), mmul(rotate_y(atan2f(a->dx, a->dz)), rotate_x(-raise)));
}
// Alternate lamps of each signal take turns, about once a second.
static bool rail_lamp_lit(const Game *g, int lamp) {
    return g->rail.warning && (fmodf(g->rail.time, 1.f) < .5f) == (lamp == 0);
}
static void draw_railway(Renderer *r, const Game *g, bool depth) {
    const Railway *rail = &g->tracks[g->selected].rail;
    if (!rail->count)
        return;
    RailVehicle vehicles[MAX_RAIL_VEHICLES];
    int count = rail_vehicles(g, r->alpha, vehicles);
    for (int i = 0; i < count; i++)
        if (vehicles[i].visible)
            draw_mesh(r, r->rail_models[vehicles[i].model], rail_vehicle_model(&vehicles[i]),
                      TRACK_INFO[g->selected].livery, depth);
    // Striped arms swing down from upright about the hinge on each housing.
    float raise = (1 - rail_arm(g, r->alpha)) * PI * .5f;
    for (int i = 0; i < rail->arm_count; i++) {
        const RailArm *a = &rail->arms[i];
        Mat4 base = rail_arm_model(a, raise);
        draw_mesh(r, r->cube, mmul(base, scale3(.16f, .16f, .16f)), rgb(.20f, .21f, .22f), depth);
        int stripes = (int)ceilf(a->length / .55f);
        float step = a->length / stripes;
        for (int k = 0; k < stripes; k++) {
            Mat4 m = mmul(base, mmul(translate(v3(0, 0, step * (k + .5f))),
                                     scale3(.055f, .07f, step * .5f)));
            draw_mesh(r, r->cube, m, k % 2 ? rgb(.94f, .93f, .90f) : rgb(.86f, .10f, .07f), depth);
        }
        if (!depth && g->rail.arm > .02f) {
            glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 1);
            Mat4 tip = mmul(base, mmul(translate(v3(0, .1f, a->length - .1f)), scale3(.06f, .06f, .06f)));
            draw_mesh(r, r->cube, tip, rgb(1, .14f, .06f), false);
            glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
        }
    }
    if (depth || !g->rail.warning)
        return;
    glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 1);
    for (int i = 0; i < rail->light_count; i++) {
        const RailLight *l = &rail->lights[i];
        Vec3 right = v3(l->dz, 0, -l->dx);
        for (int lamp = 0; lamp < 2; lamp++) {
            if (!rail_lamp_lit(g, lamp))
                continue;
            Vec3 p = vadd(l->pos, vmul(right, lamp ? l->spread : -l->spread));
            Mat4 m = mmul(translate(p), mmul(rotate_y(atan2f(l->dx, l->dz)), scale3(.13f, .13f, .02f)));
            draw_mesh(r, r->cube, m, rgb(1, .12f, .05f), false);
        }
    }
    glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
}
// Halos for the lit signal lamps, the arm tips and the train's headlamps.
static int rail_glows(const Renderer *r, const Game *g, float *data) {
    const Railway *rail = &g->tracks[g->selected].rail;
    int count = 0;
    if (!rail->count)
        return 0;
    for (int i = 0; i < rail->light_count; i++) {
        const RailLight *l = &rail->lights[i];
        Vec3 right = v3(l->dz, 0, -l->dx);
        for (int lamp = 0; lamp < 2; lamp++) {
            if (!rail_lamp_lit(g, lamp))
                continue;
            Vec3 p = vadd(vadd(l->pos, vmul(right, lamp ? l->spread : -l->spread)),
                          v3(l->dx * .08f, 0, l->dz * .08f));
            float v[] = {p.x, p.y, p.z, .7f, 1, 4.f, 0, 0};
            memcpy(data + count++ * 8, v, sizeof v);
        }
    }
    float raise = (1 - rail_arm(g, r->alpha)) * PI * .5f;
    for (int i = 0; g->rail.arm > .02f && i < rail->arm_count; i++) {
        Vec3 p = transform_point(rail_arm_model(&rail->arms[i], raise),
                                 v3(0, .1f, rail->arms[i].length - .1f));
        float v[] = {p.x, p.y, p.z, .35f, 1, 4.f, 0, 0};
        memcpy(data + count++ * 8, v, sizeof v);
    }
    RailVehicle vehicles[MAX_RAIL_VEHICLES];
    int n = rail_vehicles(g, r->alpha, vehicles);
    if (n && vehicles[0].visible) {
        Mat4 m = rail_vehicle_model(&vehicles[0]);
        int lamps = vehicles[0].model == RAIL_MODEL_ROAD_TRAIN_TRACTOR ? 1 : 3;
        for (int k = 0; k < lamps && count < RAIL_HALOS; k++) {
            Vec3 p = transform_point(m, RAIL_HEADLAMPS[vehicles[0].model][k]);
            float v[] = {p.x, p.y, p.z, .75f, 1, 3.f, 0, 0};
            memcpy(data + count++ * 8, v, sizeof v);
        }
    }
    return count;
}

// One beam per car between its headlamps; the shader derives the tail lamps.
static void lamp_uniforms(const Renderer *r) {
    float lamps[CAR_COUNT * 4] = {0}, directions[CAR_COUNT * 4] = {0};
    for (int i = 0; i < CAR_COUNT && r->night > 0; i++) {
        if (!r->car_visible[i])
            continue;
        Mat4 m = r->car_models[i];
        Vec3 front = car_lamp(r->car_meshes[i], 0);
        Vec3 p = transform_point(m, v3(0, front.y, front.z + .08f));
        Vec3 f = vnorm(v3(m.m[8], m.m[9], m.m[10]));
        float lamp[] = {p.x, p.y, p.z, 1}, direction[] = {f.x, f.y, f.z, 0};
        memcpy(lamps + i * 4, lamp, sizeof lamp);
        memcpy(directions + i * 4, direction, sizeof direction);
    }
    glUniform4fv(glGetUniformLocation(r->shader, "uLamps[0]"), CAR_COUNT, lamps);
    glUniform4fv(glGetUniformLocation(r->shader, "uLampDirs[0]"), CAR_COUNT, directions);
}

// Additive halos over the headlamps and tail lamps, after the translucent spray.
static void draw_lamp_glows(Renderer *r, const Game *g) {
    float data[(CAR_COUNT * 4 + RAIL_HALOS) * 8];
    int count = rail_glows(r, g, data);
    for (int i = 0; i < CAR_COUNT; i++) {
        if (!r->car_visible[i])
            continue;
        for (int k = 0; k < 4; k++) {
            bool tail = k >= 2;
            Vec3 lens = car_lamp(r->car_meshes[i], k);
            lens.z += tail ? -.1f : .2f;
            Vec3 p = transform_point(r->car_models[i], lens);
            float v[] = {p.x, p.y, p.z, tail ? .42f : .62f, 1, tail ? 4.f : 3.f, 0, 0};
            memcpy(data + count++ * 8, v, sizeof v);
        }
    }
    if (!count)
        return;
    GLuint p = r->spray_shader;
    glUseProgram(p);
    uniform_matrix(p, "uVP", r->vp);
    uniform_vec(p, "uRight", v3(r->view.m[0], r->view.m[4], r->view.m[8]));
    uniform_vec(p, "uUp", v3(r->view.m[1], r->view.m[5], r->view.m[9]));
    glBindVertexArray(r->spray_vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->spray_vbo);
    // Halos use their own region after the spray instances; point the attributes at it.
    size_t base = MAX_PARTICLES * 8 * sizeof(float);
    glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)base, count * 8 * sizeof(float), data);
    for (int i = 0; i < 2; i++)
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              (void *)(uintptr_t)(base + i * 4 * sizeof(float)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
    r->draw_calls++;
    r->triangles += count * 2;
    for (int i = 0; i < 2; i++)
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              (void *)(uintptr_t)(i * 4 * sizeof(float)));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    r->bound_vao = 0;
}

static void render_objects(Renderer *r, const Game *g, bool depth, bool menu, bool garage) {
    Color white = rgb(1, 1, 1);
    Track const *t = &g->tracks[g->selected];
    if (garage) {
        Mat4 platform = mmul(translate(v3(0, -.18f, 0)), scale3(3.5f, .16f, 3.5f));
        draw_mesh(r, r->cube, platform, rgb(.17f, .18f, .20f), depth);
        Mat4 rim = mmul(translate(v3(0, -.36f, 0)), scale3(3.62f, .05f, 3.62f));
        draw_mesh(r, r->cube, rim, rgb(.89f, .29f, .12f), depth);
        Mat4 model = mmul(rotate_y(g->clock * .20f + .5f), scale3(1.65f, 1.65f, 1.65f));
        set_gloss(r, depth, 1);
        draw_mesh(r, r->cars[g->garage_car], model, CAR_COLORS[g->profile.color], depth);
        set_gloss(r, depth, 0);
        return;
    }
    draw_tiled(r, &r->scene.scene, white, depth);
    draw_tiled(r, &r->scene.land, white, depth);
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
    draw_tiled(r, &r->scene.vegetation, white, depth);
    glUniform1i(glGetUniformLocation(program, "uVegetation"), 0);
    glUniform1i(glGetUniformLocation(program, "uCrowd"), 1);
    glUniform1f(glGetUniformLocation(program, "uCrowdTime"), r->crowd_time);
    glUniform1f(glGetUniformLocation(program, "uWind"), menu ? 0 : vlen(g->weather.wind));
    draw_tiled(r, &r->scene.crowd, white, depth);
    glUniform1i(glGetUniformLocation(program, "uCrowd"), 0);
    glUniform1i(glGetUniformLocation(program, "uWildlife"), 1);
    draw_tiled(r, &r->scene.wildlife, white, depth);
    glUniform1i(glGetUniformLocation(program, "uWildlife"), 0);
    if (!menu)
        draw_props(r, g, depth);
    set_gloss(r, depth, .5f);
    draw_railway(r, g, depth);
    set_gloss(r, depth, .7f);
    if (!menu)
        draw_helicopter(r, g, depth);
    set_gloss(r, depth, 1);
    for (int i = 0; i < CAR_COUNT; i++)
        if (r->car_visible[i])
            draw_mesh(r, r->cars[r->car_meshes[i]], r->car_models[i], r->car_colors[i], depth);
    set_gloss(r, depth, 0);
    if (!depth && r->night > 0) {
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 1);
        for (int i = 0; i < CAR_COUNT; i++)
            if (r->car_visible[i])
                draw_mesh(r, r->lamps[r->car_meshes[i]], r->car_models[i], white, false);
        // Fuel cans glow softly so they can still be planned for in the dark.
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), .45f * r->night);
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
        set_gloss(r, depth, .6f);
        draw_mesh(r, r->fuel, model, white, depth);
    }
    set_gloss(r, depth, 0);
    if (!depth)
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    if (!depth) {
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), .30f * (1 - r->night));
        for (int i = 0; i < MAX_PARTICLES; i++) {
            const Particle *p = &g->particles[i];
            if (p->life <= 0 || (p->kind != PARTICLE_DUST && p->kind != PARTICLE_SMOKE))
                continue;
            float age = 1 - p->life / p->max_life;
            // Smoke billows out, then thins away; dust only shrinks.
            float s = p->kind == PARTICLE_SMOKE ? p->size * (1 + 2.2f * age) * fminf(1, (1 - age) * 4)
                                                : p->size * (.3f + .7f * p->life / p->max_life);
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
    const TrackInfo *info = &TRACK_INFO[g->selected];
    uniform_vec(p, "uFlakes", v3(info->flakes.r, info->flakes.g, info->flakes.b));
    uniform_vec(p, "uGustA", info->gust[0]);
    uniform_vec(p, "uGustB", info->gust[1]);
    glUniform1f(glGetUniformLocation(p, "uNight"), r->night);
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
    float amount = TRACK_INFO[g->selected].clouds;
    glUniform1f(glGetUniformLocation(program, "uCloudAmount"), garage ? 0 : amount);
}

static void sky_uniforms(GLuint program, const Renderer *r) {
    uniform_vec(program, "uHorizon", r->horizon);
    uniform_vec(program, "uZenith", r->zenith);
    uniform_vec(program, "uEmber", r->ember);
    glUniform1f(glGetUniformLocation(program, "uOvercast"), r->overcast);
    glUniform1f(glGetUniformLocation(program, "uNight"), r->night);
}

// Fills the background left after the opaque scene; depth stays at the far plane.
static void draw_sky(Renderer *r, const Game *g, bool calm) {
    GLuint p = r->sky_shader;
    glUseProgram(p);
    sky_uniforms(p, r);
    cloud_uniforms(p, r, g, calm, false);
    uniform_vec(p, "uEye", r->eye);
    uniform_vec(p, "uRight", v3(r->view.m[0], r->view.m[4], r->view.m[8]));
    uniform_vec(p, "uUp", v3(r->view.m[1], r->view.m[5], r->view.m[9]));
    uniform_vec(p, "uBack", v3(r->view.m[2], r->view.m[6], r->view.m[10]));
    glUniform4f(glGetUniformLocation(p, "uProj"), r->projection.m[0], r->projection.m[5],
                r->projection.m[8], r->projection.m[9]);
    glDepthMask(GL_FALSE);
    glBindVertexArray(r->empty_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    r->bound_vao = 0;
    r->draw_calls++;
    r->triangles++;
}

static void post_pass(GLuint fbo, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Ambient occlusion from the scene depth, the bloom pyramid from its color and,
// for the menu's lens, the depth-of-field image.
static void post_effects(Renderer *r) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(r->empty_vao);
    glActiveTexture(GL_TEXTURE0);
    int hw = (r->width + 1) / 2, hh = (r->height + 1) / 2;
    GLuint p = r->ao_shader;
    glUseProgram(p);
    glBindTexture(GL_TEXTURE_2D, r->scene_depth);
    glUniform1i(glGetUniformLocation(p, "uDepth"), 0);
    glUniform4f(glGetUniformLocation(p, "uProj"), r->projection.m[0], r->projection.m[5],
                r->projection.m[8], r->projection.m[9]);
    glUniform2f(glGetUniformLocation(p, "uDepthParams"), r->projection.m[10], r->projection.m[14]);
    glUniform4f(glGetUniformLocation(p, "uViewport"), (float)r->viewport[0] / r->width,
                (float)r->viewport[1] / r->height, (float)r->viewport[2] / r->width,
                (float)r->viewport[3] / r->height);
    glUniform2f(glGetUniformLocation(p, "uSize"), (float)r->width, (float)r->height);
    // Contact shadows within about a metre: under cars, around trunks, rocks and curbs.
    float radius = 1.1f;
    glUniform1f(glGetUniformLocation(p, "uRadius"), radius);
    glUniform1f(glGetUniformLocation(p, "uScale"), r->projection.m[5] * .5f * r->viewport[3]);
    glUniform1f(glGetUniformLocation(p, "uMaxPixels"), r->height * .06f);
    glUniform1f(glGetUniformLocation(p, "uIntensity"), .8f);
    glUniform1f(glGetUniformLocation(p, "uFogFar"), r->fog_far);
    post_pass(r->ao_fbo[0], hw, hh);
    p = r->ao_blur_shader;
    glUseProgram(p);
    glBindTexture(GL_TEXTURE_2D, r->ao_texture[0]);
    glUniform1i(glGetUniformLocation(p, "uOcclusion"), 0);
    post_pass(r->ao_fbo[1], hw, hh);
    p = r->bloom_down_shader;
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "uSource"), 0);
    bool hdr = r->scene_format != GL_RGBA8;
    glUniform1f(glGetUniformLocation(p, "uThreshold"), hdr ? 1 : .82f);
    for (int i = 0; i < BLOOM_LEVELS; i++) {
        glBindTexture(GL_TEXTURE_2D, i ? r->bloom_texture[i - 1] : r->scene_texture);
        float sw = i ? bloom_size(r->width, i - 1) : r->width;
        float sh = i ? bloom_size(r->height, i - 1) : r->height;
        glUniform2f(glGetUniformLocation(p, "uTexel"), 1 / sw, 1 / sh);
        glUniform1i(glGetUniformLocation(p, "uPrefilter"), i == 0);
        post_pass(r->bloom_fbo[i], bloom_size(r->width, i), bloom_size(r->height, i));
    }
    // Each level adds the blurred level below it, so level 0 holds every radius.
    p = r->bloom_up_shader;
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "uSource"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = BLOOM_LEVELS - 2; i >= 0; i--) {
        glBindTexture(GL_TEXTURE_2D, r->bloom_texture[i + 1]);
        glUniform2f(glGetUniformLocation(p, "uTexel"), 1.f / bloom_size(r->width, i + 1),
                    1.f / bloom_size(r->height, i + 1));
        post_pass(r->bloom_fbo[i], bloom_size(r->width, i), bloom_size(r->height, i));
    }
    glDisable(GL_BLEND);
    if (r->aperture > 0) {
        p = r->dof_down_shader;
        glUseProgram(p);
        GLuint inputs[] = {r->scene_texture, r->scene_depth};
        const char *names[] = {"uScene", "uDepth"};
        for (int i = 0; i < 2; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, inputs[i]);
            glUniform1i(glGetUniformLocation(p, names[i]), i);
        }
        glUniform2f(glGetUniformLocation(p, "uDepthParams"), r->projection.m[10],
                    r->projection.m[14]);
        glUniform1f(glGetUniformLocation(p, "uFocus"), r->focus);
        post_pass(r->dof_fbo[0], hw, hh);
        p = r->dof_blur_shader;
        glUseProgram(p);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->dof_texture[0]);
        glUniform1i(glGetUniformLocation(p, "uSource"), 0);
        glUniform1f(glGetUniformLocation(p, "uRadius"), r->aperture * hh);
        post_pass(r->dof_fbo[1], hw, hh);
    }
    r->bound_vao = 0;
}

void renderer_present(Renderer *r) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(r->empty_vao);
    GLuint p = r->post_shader;
    glUseProgram(p);
    GLuint textures[] = {r->scene_texture, r->ao_texture[1], r->bloom_texture[0], r->scene_depth,
                         r->dof_texture[1]};
    const char *names[] = {"uScene", "uOcclusion", "uBloom", "uDepth", "uDof"};
    for (int i = 0; i < 5; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glUniform1i(glGetUniformLocation(p, names[i]), i);
    }
    glUniform2f(glGetUniformLocation(p, "uResolution"), (float)r->width, (float)r->height);
    glUniform2f(glGetUniformLocation(p, "uOcclusionTexel"), 1.f / ((r->width + 1) / 2),
                1.f / ((r->height + 1) / 2));
    glUniform1f(glGetUniformLocation(p, "uVignette"), r->vignette);
    glUniform1f(glGetUniformLocation(p, "uFade"), r->fade);
    glUniform1f(glGetUniformLocation(p, "uSpeedBlur"), r->speed_blur);
    glUniform1f(glGetUniformLocation(p, "uAperture"), r->aperture);
    glUniform1f(glGetUniformLocation(p, "uFocus"), r->focus);
    glUniform2f(glGetUniformLocation(p, "uDepthParams"), r->projection.m[10], r->projection.m[14]);
    // The summed pyramid levels: a faint daytime sheen, stronger halos at night.
    float bloom = (.018f + .045f * r->night) * (r->scene_format != GL_RGBA8 ? 1 : .6f);
    glUniform1f(glGetUniformLocation(p, "uBloomStrength"), bloom);
    post_pass(r->ldr_fbo, r->width, r->height);
    p = r->fxaa_shader;
    glUseProgram(p);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->ldr_texture);
    glUniform1i(glGetUniformLocation(p, "uImage"), 0);
    glUniform2f(glGetUniformLocation(p, "uResolution"), (float)r->width, (float)r->height);
    post_pass(r->default_fbo, r->width, r->height);
    r->bound_vao = 0;
}

static void draw_water(Renderer *r, const Game *g, int vx, int vy, int vw, int vh) {
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
    sky_uniforms(p, r);
    cloud_uniforms(p, r, g, r->hero, false);
    glUniform1f(glGetUniformLocation(p, "uTime"), r->crowd_time);
    const TrackInfo *info = &TRACK_INFO[g->selected];
    uniform_vec(p, "uWaterTint", info->water_tint);
    uniform_vec(p, "uWaterExtinction", info->water_extinction);
    // Only liquid above the sea is lava; the sea stays water.
    glUniform1f(glGetUniformLocation(p, "uLavaAbove"), info->lava ? LAVA_ABOVE : 1e9f);
    glUniform1f(glGetUniformLocation(p, "uClipFar"), r->clip_far);
    glUniform2f(glGetUniformLocation(p, "uResolution"), r->width, r->height);
    glUniform2f(glGetUniformLocation(p, "uDepthParams"), r->projection.m[10], r->projection.m[14]);
    glUniform4f(glGetUniformLocation(p, "uViewport"), (float)vx/r->width, (float)vy/r->height,
                (float)vw/r->width, (float)vh/r->height);
    float wakes[CAR_COUNT * 4] = {0};
    for (int i = 0; i < CAR_COUNT && !r->hero; i++) {
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
    Mesh m = r->scene.water;
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
    glUniform1f(glGetUniformLocation(p, "uNight"), r->night);
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

// The main menu films the pack like a race broadcast, cutting between four
// angles on a fixed beat. Returns how far each cut dips to black.
static float hero_camera(Renderer *r, const Game *g, float aspect, float *blur) {
    const Track *t = &g->tracks[g->selected];
    int shot = (int)(g->clock / HERO_SHOT) % 4;
    float local = fmodf(g->clock, HERO_SHOT);
    float player = hero_distance(g, 0), lead = hero_distance(g, 1);
    float middle = lead - HERO_GAP * (CAR_COUNT - 1) * .5f;
    float fov = 50;
    *blur = 0;
    // Each angle has its own lens: the telephoto and low shots blur the most.
    static const float APERTURE[4] = {.004f, .006f, .010f, .010f};
    r->aperture = APERTURE[shot];
    if (shot == 0) {
        // Chasing the player's bumper, looking up the road at the leaders.
        float lane = hero_lane(g, 0);
        r->eye = vadd(track_at(t, player - 10.5f, lane, NULL), v3(0, 3.4f, 0));
        r->target = vadd(track_at(t, player + 18, lane, NULL), v3(0, .3f, 0));
        r->focus = vlen(vsub(track_at(t, player, lane, NULL), r->eye));
        *blur = .035f;
    } else if (shot == 1) {
        // A helicopter circling the bunch; each pass starts from a new side.
        Vec3 c = track_at(t, middle, 0, NULL);
        float a = (int)(g->clock / (HERO_SHOT * 4)) * 2.4f + local * .16f;
        r->eye = vadd(c, v3(sinf(a) * 27, 13, cosf(a) * 27));
        r->target = vadd(c, v3(0, 1, 0));
        r->focus = vlen(vsub(r->target, r->eye));
        fov = 46;
    } else if (shot == 2) {
        // Low on the tarmac ahead of the leader, with the field charging in.
        r->eye = vadd(track_at(t, lead + 7.5f, .6f, NULL), v3(0, 1.15f, 0));
        r->target = vadd(track_at(t, lead - 16, 0, NULL), v3(0, 1.1f, 0));
        r->focus = vlen(vsub(track_at(t, lead, hero_lane(g, 1), NULL), r->eye));
        fov = 42;
        *blur = .03f;
    } else {
        // A trackside camera on the outside of the next bend; the pack sweeps past.
        float s = (g->clock - local) * HERO_SPEED + BROADCAST_START + 64;
        Vec3 ahead = track_at(t, s + 22, 0, NULL), sides[2];
        for (int i = 0; i < 2; i++)
            sides[i] = track_at(t, s, i ? 13.f : -13.f, NULL);
        Vec3 p = vlen(vsub(sides[0], ahead)) > vlen(vsub(sides[1], ahead)) ? sides[0] : sides[1];
        float ground = track_height(t, vadd(p, v3(0, 8, 0)), NULL);
        p.y = fmaxf(p.y + 3.2f, ground + 1.8f);
        r->eye = p;
        r->target = vadd(track_at(t, middle + 4, 0, NULL), v3(0, .8f, 0));
        r->focus = vlen(vsub(r->target, r->eye));
        fov = 30;
    }
    r->clip_far = 550;
    r->projection = perspective(fov * PI / 180, aspect, .3f, r->clip_far);
    // Shift the lens so the cars sit beside the menu panel, above the track cards.
    r->projection.m[8] -= r->hero_frame;
    r->projection.m[9] -= .12f;
    return 1 - clampf(fminf(local, HERO_SHOT - local) / .2f, 0, 1);
}

void renderer_draw(Renderer *r, const Game *g, float dt, float alpha) {
    renderer_load_track(r, g->selected);
    if (g->screen != SCREEN_PAUSE)
        r->crowd_time += clampf(dt, 0, .1f);
    bool menu =
        g->screen == SCREEN_MENU || g->screen == SCREEN_HELP || g->screen == SCREEN_SETTINGS ||
        g->screen == SCREEN_RECORDS || g->screen == SCREEN_RESET_DATA;
    bool garage = g->screen == SCREEN_GARAGE, calm = menu || garage;
    // Menu pages run over a full-screen broadcast of the selected track.
    r->hero = menu;
    if (!calm)
        update_helicopter(r, g, dt);
    else
        r->helicopter_ready = false;
    const Track *t = &g->tracks[g->selected];
    r->alpha = alpha;
    r->draw_calls = 0;
    r->triangles = 0;
    r->bound_vao = 0;
    // The garage stays a daylight studio; menus preview the selected race.
    r->night = game_night(g) && !garage ? 1.f : 0.f;
    // Lava lights the haze and the ground warmly from below after dark.
    r->ember = TRACK_INFO[g->selected].lava && !garage ? v3(.22f, .07f, .025f) : v3(0, 0, 0);
    r->sky = t->sky;
    if (r->night > 0)
        r->sky = rgb(.012f + t->sky.r * .07f, .02f + t->sky.g * .08f, .045f + t->sky.b * .1f);
    // The garage is a dark showroom matching the menus.
    Color clear = garage ? rgb(.035f, .048f, .062f) : r->sky;
    float overcast = calm || g->weather.kind == WEATHER_WIND ? 0 : g->weather.intensity;
    Color haze = r->night > 0 ? rgb(.10f, .12f, .15f) : rgb(.53f, .61f, .69f);
    clear.r = lerpf(clear.r, haze.r, overcast * .75f);
    clear.g = lerpf(clear.g, haze.g, overcast * .75f);
    clear.b = lerpf(clear.b, haze.b, overcast * .75f);
    // Each circuit's haze is the horizon of a deeper sky; the garage car mirrors
    // a studio's overhead softbox instead.
    Vec3 zenith = r->night > 0 ? v3(.006f, .012f, .034f) : TRACK_INFO[g->selected].zenith;
    zenith = vlerp(zenith, vmul(v3(haze.r, haze.g, haze.b), .92f), overcast * .85f);
    r->horizon = v3(clear.r, clear.g, clear.b);
    r->zenith = garage ? v3(.62f, .65f, .70f) : zenith;
    r->overcast = overcast;
    int vx = 0, vy = 0, vw = r->width, vh = r->height;
    if (garage) {
        // Above the upgrade rows, right of the car's name.
        vx = (int)(r->width * .37f);
        vy = (int)(r->height * .33f);
        vw = r->width - vx;
        vh = (int)(r->height * .60f);
    }
    float aspect = (float)vw / vh, fade = 0, blur = 0;
    r->aperture = 0;
    if (menu) {
        fade = hero_camera(r, g, aspect, &blur);
    } else if (garage) {
        r->eye = v3(7.9f, 6.1f, 10);
        r->target = v3(0, .7f, 0);
        r->clip_far = 100;
        r->projection = perspective(38 * PI / 180, aspect, .1f, r->clip_far);
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
        // Trailing beyond the island's edge, the camera stays above the land there.
        r->eye.y = fmaxf(r->eye.y, land_top(r, r->eye) + 8);
        if (g->shake > 0)
            r->eye.x += sinf(g->clock * 95) * g->shake * .8f;
        r->clip_far = 300;
        r->projection = perspective(49 * PI / 180, aspect, .5f, r->clip_far);
    }
    r->view = lookat(r->eye, r->target, v3(0, 1, 0));
    r->vp = mmul(r->projection, r->view);
    Vec3 light_target = garage ? v3(0, 0, 0) : r->target;
    float shadow_span = garage ? 9 : menu ? 80 : 65;
    // The frustum follows the camera in whole shadow-map texels, so shadow edges
    // stay still instead of crawling as the car drives.
    Mat4 light_view = lookat(v3(-180, 300, 135), v3(0, 0, 0), v3(0, 1, 0));
    Vec3 light_center = transform_point(light_view, light_target);
    float texel = 2 * shadow_span / 2048;
    light_center.x = roundf(light_center.x / texel) * texel;
    light_center.y = roundf(light_center.y / texel) * texel;
    r->light_vp = mmul(ortho(light_center.x - shadow_span, light_center.x + shadow_span,
                             light_center.y - shadow_span, light_center.y + shadow_span,
                             -light_center.z - 374, -light_center.z + 275),
                       light_view);
    if (!garage)
        update_cars(r, g, menu);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, r->shadow_fbo);
    glViewport(0, 0, 2048, 2048);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(r->depth_shader);
    uniform_matrix(r->depth_shader, "uVP", r->light_vp);
    set_frustum(r, r->light_vp);
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
    set_frustum(r, r->vp);
    uniform_matrix(r->shader, "uLight", r->light_vp);
    uniform_vec(r->shader, "uEye", r->eye);
    cloud_uniforms(r->shader, r, g, calm, garage);
    sky_uniforms(r->shader, r);
    // Night haze closes in a little so distant scenery settles into the dark.
    r->fog_far = garage ? 1400 : (190 - 65 * overcast) * (1 - .12f * r->night) * TRACK_INFO[g->selected].fog;
    glUniform1f(glGetUniformLocation(r->shader, "uFogFar"), r->fog_far);
    glUniform1f(glGetUniformLocation(r->shader, "uClipFar"), r->clip_far);
    glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    glUniform1f(glGetUniformLocation(r->shader, "uGloss"), 0);
    // Road material colors from tools/build_assets.py.
    uniform_vec(r->shader, "uRoad", TRACK_INFO[g->selected].road);
    lamp_uniforms(r);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->shadow_texture);
    glUniform1i(glGetUniformLocation(r->shader, "uShadow"), 0);
    render_objects(r, g, false, menu, garage);
    if (!garage) {
        draw_sky(r, g, calm);
        draw_water(r, g, vx, vy, vw, vh);
    }
    if (!calm)
        draw_spray(r, g);
    if (!garage && r->night > 0)
        draw_lamp_glows(r, g);
    if (!calm)
        draw_weather(r, g);
    int viewport[] = {vx, vy, vw, vh};
    memcpy(r->viewport, viewport, sizeof viewport);
    post_effects(r);
    r->vignette = .3f + .2f * r->night;
    r->fade = fade;
    const Car *player = &g->cars[0];
    float pace = clampf((player->speed - 16.f) / 24.f, 0, 1);
    pace = pace * pace * (3 - 2 * pace);
    if (g->screen == SCREEN_RACE && !player->dnf && !player->finished)
        blur = pace * (.065f + .025f * clampf(player->boost_cooldown / 2, 0, 1));
    r->speed_blur = blur;
    renderer_present(r);
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
    scene_delete(&r->scene);
    for (int i = 0; i < TRACK_COUNT; i++)
        mesh_delete(&r->boosts[i]);
    mesh_delete(&r->cone);
    mesh_delete(&r->tire);
    mesh_delete(&r->fence);
    for (int i = 0; i < CAR_MODEL_COUNT; i++) {
        mesh_delete(&r->lamps[i]);
        mesh_delete(&r->cars[i]);
    }
    mesh_delete(&r->fuel);
    mesh_delete(&r->helicopter);
    mesh_delete(&r->helicopter_rotor);
    mesh_delete(&r->helicopter_tail_rotor);
    mesh_delete(&r->cube);
    glDeleteProgram(r->shader);
    glDeleteProgram(r->depth_shader);
    glDeleteProgram(r->post_shader);
    glDeleteProgram(r->weather_shader);
    glDeleteProgram(r->water_shader);
    glDeleteProgram(r->spray_shader);
    glDeleteProgram(r->sky_shader);
    glDeleteProgram(r->ao_shader);
    glDeleteProgram(r->ao_blur_shader);
    glDeleteProgram(r->bloom_down_shader);
    glDeleteProgram(r->bloom_up_shader);
    glDeleteProgram(r->fxaa_shader);
    glDeleteFramebuffers(2, r->ao_fbo);
    glDeleteTextures(2, r->ao_texture);
    glDeleteFramebuffers(BLOOM_LEVELS, r->bloom_fbo);
    glDeleteTextures(BLOOM_LEVELS, r->bloom_texture);
    glDeleteFramebuffers(1, &r->ldr_fbo);
    glDeleteTextures(1, &r->ldr_texture);
    glDeleteProgram(r->dof_down_shader);
    glDeleteProgram(r->dof_blur_shader);
    glDeleteFramebuffers(2, r->dof_fbo);
    glDeleteTextures(2, r->dof_texture);
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
    glDeleteTextures(1, &r->scene_depth);
}
