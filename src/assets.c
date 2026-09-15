#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *asset_read(const char *name,size_t *size){
    char path[2048];
#ifdef SDL_PLATFORM_ANDROID
    // SDL maps relative paths directly into the APK's AssetManager root.
    void *apk=SDL_LoadFile(name,size);if(apk)return apk;
#endif
    const char *base=SDL_GetBasePath();
    if(base){SDL_snprintf(path,sizeof path,"%sassets/%s",base,name);void *d=SDL_LoadFile(path,size);if(d)return d;}
    SDL_snprintf(path,sizeof path,"assets/%s",name);void *d=SDL_LoadFile(path,size);if(d)return d;
    SDL_snprintf(path,sizeof path,"../assets/%s",name);return SDL_LoadFile(path,size);
}

static uint32_t read_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float read_float(const unsigned char *p){uint32_t n=read_u32(p);float v;memcpy(&v,&n,4);return v;}

bool game_load(Game *g){
    memset(g,0,sizeof *g);g->rng=81291;
    const char *ids[]={"country","beach","winter"};
    const char *names[]={"Harvest Hills","Sunshine Coast","Alpine Rush"};
    const char *sub[]={"COUNTRY RUN","BEACH ESCAPE","WINTER CIRCUIT"};
    const char *descs[]={"Rolling hills. Red barns. Full throttle.","Salt in the air. Sand in your tires.","Fresh powder. Slippery turns. Big air."};
    Color accents[]={rgb(.86,.30,.13),rgb(.04,.49,.51),rgb(.30,.45,.69)};
    Color skies[]={rgb(.84,.87,.74),rgb(.63,.83,.85),rgb(.72,.81,.88)};
    for(int k=0;k<TRACK_COUNT;k++){
        Track *t=&g->tracks[k];t->id=ids[k];t->name=names[k];t->subtitle=sub[k];t->description=descs[k];t->accent=accents[k];t->sky=skies[k];
        char name[100];SDL_snprintf(name,sizeof name,"tracks/%s.tcp",ids[k]);size_t size;unsigned char *d=asset_read(name,&size);
        if(!d||size<20||memcmp(d,"TCP1",4)){SDL_free(d);SDL_Log("Missing or invalid track: %s",name);return false;}
        uint32_t n=read_u32(d+4),nc=read_u32(d+8),nr=read_u32(d+12);
        if(n<32||n>MAX_POINTS||nc>MAX_CANS||nr>MAX_RAMPS||size!=20+n*28+nc*8+nr*16){SDL_free(d);return false;}
        t->count=(int)n;t->can_count=(int)nc;t->ramp_count=(int)nr;t->length=read_float(d+16);
        if(!isfinite(t->length)||t->length<100||t->length>10000){SDL_free(d);return false;}
        size_t off=20;float lo=10000,hi=-10000;
        for(int i=0;i<t->count;i++){
            float v[7];for(int j=0;j<7;j++,off+=4){v[j]=read_float(d+off);if(!isfinite(v[j])){SDL_free(d);return false;}}
            if(v[5]<2||v[5]>30||v[6]<0||v[6]>=t->length||(i>0&&v[6]<=t->points[i-1].s)){SDL_free(d);return false;}
            t->points[i]=(TrackPoint){v3(v[0],v[1],v[2]),v[3],v[4],v[5],v[6]};lo=fminf(lo,v[1]);hi=fmaxf(hi,v[1]);
        }
        t->elevation=hi-lo;
        for(int i=0;i<t->can_count;i++,off+=8){t->cans[i]=(Can){read_float(d+off),read_float(d+off+4)};if(!isfinite(t->cans[i].index)||t->cans[i].index<0||t->cans[i].index>=n||!isfinite(t->cans[i].lane)){SDL_free(d);return false;}}
        for(int i=0;i<t->ramp_count;i++,off+=16){Ramp r={read_float(d+off),read_float(d+off+4),read_float(d+off+8),read_float(d+off+12)};if(!isfinite(r.index)||r.index<0||r.index>=n||!isfinite(r.length)||r.length<=0||!isfinite(r.height)||r.height<0||!isfinite(r.half)||r.half<=0){SDL_free(d);return false;}t->ramps[i]=r;}
        SDL_free(d);
    }
    profile_load(&g->profile);g->screen=SCREEN_MENU;return true;
}

static bool profile_path(char *out,size_t n){char *base=SDL_GetPrefPath("ToyCars","ToyCars");if(!base)return false;SDL_snprintf(out,n,"%sprofile.txt",base);SDL_free(base);return true;}
void profile_load(Profile *p){
    *p=(Profile){.color=0,.difficulty=1,.auto_accel=true,.touch=true,.sound=true,.music=true};
    char path[2048];if(!profile_path(path,sizeof path))return;size_t size;char *d=SDL_LoadFile(path,&size);if(!d)return;
    int c,level,a,t,s,m,med0,med1,med2;float b0,b1,b2;
    int n=sscanf(d,"TOYCARS 1\n%d %d %d %d %d %d\n%f %f %f\n%d %d %d",&c,&level,&a,&t,&s,&m,&b0,&b1,&b2,&med0,&med1,&med2);
    if(n==12&&c>=0&&c<6&&level>=0&&level<3&&isfinite(b0)&&isfinite(b1)&&isfinite(b2)&&b0>=0&&b1>=0&&b2>=0){
        p->color=c;p->difficulty=level;p->auto_accel=!!a;p->touch=!!t;p->sound=!!s;p->music=!!m;
        p->best[0]=b0;p->best[1]=b1;p->best[2]=b2;p->medals[0]=(int)clampf(med0,0,3);p->medals[1]=(int)clampf(med1,0,3);p->medals[2]=(int)clampf(med2,0,3);
    }SDL_free(d);
}
void profile_save(const Profile *p){
    char path[2048],temp[2060],data[512];if(!profile_path(path,sizeof path))return;SDL_snprintf(temp,sizeof temp,"%s.tmp",path);
    int len=SDL_snprintf(data,sizeof data,"TOYCARS 1\n%d %d %d %d %d %d\n%.3f %.3f %.3f\n%d %d %d\n",p->color,p->difficulty,p->auto_accel,p->touch,p->sound,p->music,p->best[0],p->best[1],p->best[2],p->medals[0],p->medals[1],p->medals[2]);
    if(SDL_SaveFile(temp,data,(size_t)len)){if(rename(temp,path)!=0)SDL_Log("Cannot replace profile: %s",path);}else SDL_Log("Cannot save profile: %s",SDL_GetError());
}
