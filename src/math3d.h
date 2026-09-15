#ifndef TOYCARS_MATH3D_H
#define TOYCARS_MATH3D_H
#include <math.h>
#include <stdint.h>
#define PI 3.14159265358979323846f
typedef struct { float x,y,z; } Vec3;
typedef struct { float r,g,b,a; } Color;
typedef struct { float m[16]; } Mat4;
static inline float clampf(float x,float a,float b){return fminf(b,fmaxf(a,x));}
static inline float lerpf(float a,float b,float t){return a+(b-a)*t;}
static inline Vec3 v3(float x,float y,float z){return (Vec3){x,y,z};}
static inline Vec3 vadd(Vec3 a,Vec3 b){return v3(a.x+b.x,a.y+b.y,a.z+b.z);}
static inline Vec3 vsub(Vec3 a,Vec3 b){return v3(a.x-b.x,a.y-b.y,a.z-b.z);}
static inline Vec3 vmul(Vec3 a,float s){return v3(a.x*s,a.y*s,a.z*s);}
static inline float vdot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static inline Vec3 vcross(Vec3 a,Vec3 b){return v3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
static inline float vlen(Vec3 a){return sqrtf(vdot(a,a));}
static inline Vec3 vnorm(Vec3 a){float l=vlen(a);return l>1e-6f?vmul(a,1/l):v3(0,1,0);}
static inline Vec3 vlerp(Vec3 a,Vec3 b,float t){return vadd(a,vmul(vsub(b,a),t));}
static inline float angle_delta(float a,float b){return atan2f(sinf(a-b),cosf(a-b));}
static inline Color rgb(float r,float g,float b){return (Color){r,g,b,1};}
static inline Color rgba(float r,float g,float b,float a){return (Color){r,g,b,a};}
static inline Mat4 identity(void){return (Mat4){{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};}
static inline Mat4 mmul(Mat4 a,Mat4 b){Mat4 c={0};for(int col=0;col<4;col++)for(int row=0;row<4;row++)for(int k=0;k<4;k++)c.m[col*4+row]+=a.m[k*4+row]*b.m[col*4+k];return c;}
static inline Mat4 translate(Vec3 p){Mat4 m=identity();m.m[12]=p.x;m.m[13]=p.y;m.m[14]=p.z;return m;}
static inline Mat4 scale3(float x,float y,float z){Mat4 m=identity();m.m[0]=x;m.m[5]=y;m.m[10]=z;return m;}
static inline Mat4 rotate_y(float a){Mat4 m=identity();float c=cosf(a),s=sinf(a);m.m[0]=c;m.m[2]=-s;m.m[8]=s;m.m[10]=c;return m;}
static inline Mat4 rotate_x(float a){Mat4 m=identity();float c=cosf(a),s=sinf(a);m.m[5]=c;m.m[6]=s;m.m[9]=-s;m.m[10]=c;return m;}
static inline Mat4 rotate_z(float a){Mat4 m=identity();float c=cosf(a),s=sinf(a);m.m[0]=c;m.m[1]=s;m.m[4]=-s;m.m[5]=c;return m;}
static inline Mat4 perspective(float fov,float aspect,float n,float f){Mat4 m={0};float q=1/tanf(fov*.5f);m.m[0]=q/aspect;m.m[5]=q;m.m[10]=(f+n)/(n-f);m.m[11]=-1;m.m[14]=2*f*n/(n-f);return m;}
static inline Mat4 ortho(float l,float r,float b,float t,float n,float f){Mat4 m=identity();m.m[0]=2/(r-l);m.m[5]=2/(t-b);m.m[10]=-2/(f-n);m.m[12]=-(r+l)/(r-l);m.m[13]=-(t+b)/(t-b);m.m[14]=-(f+n)/(f-n);return m;}
static inline Mat4 lookat(Vec3 eye,Vec3 at,Vec3 up){Vec3 f=vnorm(vsub(at,eye)),s=vnorm(vcross(f,up)),u=vcross(s,f);Mat4 m=identity();m.m[0]=s.x;m.m[4]=s.y;m.m[8]=s.z;m.m[1]=u.x;m.m[5]=u.y;m.m[9]=u.z;m.m[2]=-f.x;m.m[6]=-f.y;m.m[10]=-f.z;m.m[12]=-vdot(s,eye);m.m[13]=-vdot(u,eye);m.m[14]=vdot(f,eye);return m;}
#endif
