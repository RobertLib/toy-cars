#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const Color CAR_COLORS[6]={{.94,.28,.10,1},{.12,.57,.60,1},{.94,.69,.16,1},{.40,.45,.76,1},{.86,.46,.59,1},{.82,.87,.77,1}};
const char *CAR_COLOR_NAMES[6]={"TANGERINE","LAGOON","HONEY","BLUEBERRY","GUAVA","VANILLA"};
static float random01(Game *g){g->rng^=g->rng<<13;g->rng^=g->rng>>17;g->rng^=g->rng<<5;return (g->rng&0xffff)/65535.f;}
static float wrap(float s,float l){s=fmodf(s,l);return s<0?s+l:s;}
static float terrain_surface(int theme,float x,float z){
    if(theme==0)return 4+4.4f*sinf(x*.025f+.8f)*cosf(z*.029f)+2.1f*sinf(z*.055f+x*.02f)-.14f;
    if(theme==1)return 3.5f+3.4f*sinf(x*.027f)*cosf(z*.031f)+1.8f*sinf(z*.038f+.6f)-.14f;
    return 8+7.2f*sinf(x*.023f+.5f)*cosf(z*.029f)+3.3f*cosf(z*.041f+x*.014f)-.14f;
}

Vec3 track_at(const Track *t,float distance,float lane,Vec3 *direction){
    float s=wrap(distance,t->length);int lo=0,hi=t->count-1;
    while(lo<hi){int mid=(lo+hi+1)/2;if(t->points[mid].s<=s)lo=mid;else hi=mid-1;}
    const TrackPoint *a=&t->points[lo],*b=&t->points[(lo+1)%t->count];
    float end=lo==t->count-1?t->length:b->s;float f=clampf((s-a->s)/(end-a->s),0,1);
    Vec3 dir=vnorm(vsub(b->p,a->p)),p=vlerp(a->p,b->p,f);if(direction)*direction=dir;
    p.x+=dir.z*lane;p.z-=dir.x*lane;return p;
}

float track_nearest(const Track *t,Vec3 p,int *index,Vec3 *center,float *lane){
    float best=1e20f,bf=0;int bi=0;int from=*index<0?0:*index-32,count=*index<0?t->count:65;
    for(int k=0;k<count;k++){
        int i=(from+k+t->count)%t->count;Vec3 a=t->points[i].p,b=t->points[(i+1)%t->count].p;
        Vec3 d=vsub(b,a),q=vsub(p,a);float den=d.x*d.x+d.z*d.z;float f=den>.0001f?clampf((q.x*d.x+q.z*d.z)/den,0,1):0;
        Vec3 c=vlerp(a,b,f);float dx=p.x-c.x,dz=p.z-c.z;float dist=dx*dx+dz*dz;
        if(dist<best){best=dist;bi=i;bf=f;}
    }
    *index=bi;const TrackPoint *a=&t->points[bi],*b=&t->points[(bi+1)%t->count];
    Vec3 c=vlerp(a->p,b->p,bf),dir=vnorm(vsub(b->p,a->p));if(center)*center=c;
    if(lane)*lane=(p.x-c.x)*dir.z-(p.z-c.z)*dir.x;
    return lerpf(a->s,bi==t->count-1?t->length:b->s,bf);
}

float track_height(const Track *t,int index,Vec3 p,float lane,bool *on_ramp){
    int idx=index;Vec3 center;track_nearest(t,p,&idx,&center,NULL);float y=center.y;
    // Match the banking used by Blender's road ribbon.
    const TrackPoint *pp=&t->points[(idx-5+t->count)%t->count],*pn=&t->points[(idx+5)%t->count];
    float bank=clampf((pp->dx*pn->dz-pp->dz*pn->dx)*.55f,-.33f,.33f);
    y+=bank*clampf(lane/t->points[idx].half,-1,1);*on_ramp=false;
    for(int j=0;j<t->ramp_count;j++){
        Ramp r=t->ramps[j];const TrackPoint *rp=&t->points[(int)r.index];Vec3 q=vsub(p,rp->p);
        float forward=q.x*rp->dx+q.z*rp->dz,side=q.x*rp->dz-q.z*rp->dx;
        if(fabsf(side)<r.half&&forward>=-r.length*.5f&&forward<=r.length*.5f){
            y=fmaxf(y,rp->p.y+(forward+r.length*.5f)/r.length*r.height);*on_ramp=true;
        }
    }return y;
}

void game_start(Game *g){
    g->screen=SCREEN_COUNTDOWN;g->time=0;g->countdown=3.4f;g->rank=8;g->finish_rank=0;g->toast_time=0;g->wrong_way=0;g->shake=0;g->last_lap=0;
    memset(g->particles,0,sizeof g->particles);g->particle_cursor=0;g->rng=81291+g->selected;
    const char *names[]={"YOU","JUNO","DASH","PIP","NOVA","ACE","RUSTY","BEAN"};Track *t=&g->tracks[g->selected];
    for(int i=0;i<CAR_COUNT;i++){
        Car *c=&g->cars[i];memset(c,0,sizeof *c);int grid=i==0?7:i-1;
        c->progress=-5-(grid/2)*4.2f;float lane=grid%2?2.0f:-2.0f;Vec3 d;
        c->pos=track_at(t,c->progress,lane,&d);c->prev=c->pos;c->yaw=atan2f(d.x,d.z);c->prev_yaw=c->yaw;
        c->grounded=true;c->fuel=100;c->nearest=-1;c->last_s=track_nearest(t,c->pos,&c->nearest,NULL,NULL);c->lap=1;
        c->target_lane=(i%3-1)*2.6f;c->color=i==6?rgb(.24f,.30f,.33f):i==7?rgb(.43f,.65f,.80f):CAR_COLORS[i==0?g->profile.color:(i+g->profile.color)%6];c->name=names[i];
    }
    g->sound_event=1;
}

void game_reset_car(Game *g,int index){
    Car *c=&g->cars[index];Track *t=&g->tracks[g->selected];Vec3 d;
    c->pos=track_at(t,c->progress,0,&d);c->prev=c->pos;c->yaw=atan2f(d.x,d.z);c->prev_yaw=c->yaw;c->velocity=v3(0,0,0);c->speed=0;c->vy=0;c->grounded=true;c->stuck_time=0;c->pitch=0;c->roll=0;c->nearest=-1;c->last_s=track_nearest(t,c->pos,&c->nearest,NULL,NULL);
    if(index==0){SDL_strlcpy(g->toast,"BACK ON TRACK",sizeof g->toast);g->toast_time=1.5f;}
}

void game_ai(const Game *g,int index,Controls *out){
    const Track *t=&g->tracks[g->selected];const Car *c=&g->cars[index];*out=(Controls){0};
    float lane=c->target_lane,look=5.5f+c->speed*.34f;
    // Plan a fuel line early. Each driver has an independent respawn timer.
    float near=1000;
    for(int j=0;j<t->can_count;j++){
        float d=wrap(t->points[(int)t->cans[j].index].s-c->last_s,t->length);
        if(d<near&&d<48&&c->can_cooldown[j]<=0&&(c->fuel<68||index==0)){near=d;lane=t->cans[j].lane;}
    }
    if(near>30){
        for(int j=0;j<CAR_COUNT;j++)if(j!=index){
            const Car *other=&g->cars[j];float d=other->progress-c->progress;
            if(d>0&&d<10&&vlen(vsub(c->pos,other->pos))<11)lane=index%2?3.0f:-3.0f;
        }
    }
    Vec3 target=track_at(t,c->last_s+look,lane,NULL);float angle=angle_delta(atan2f(target.x-c->pos.x,target.z-c->pos.z),c->yaw);
    out->steer=clampf(angle*2.15f,-1,1);
    Vec3 d0,d1;track_at(t,c->last_s+4,0,&d0);track_at(t,c->last_s+22,0,&d1);
    float curve=fabsf(angle_delta(atan2f(d1.x,d1.z),atan2f(d0.x,d0.z)));
    float desired=(27.5f-curve*13)*(g->profile.difficulty==0?.82f:g->profile.difficulty==2?1.07f:.96f);
    desired*=1.0f-(index%4)*.016f;desired=clampf(desired,10,30);
    out->throttle=c->speed<desired?1:.15f;out->brake=c->speed>desired+1.5f?.55f:0;
}

static void particle(Game *g,Vec3 p,Vec3 vel,float life,float size,Color color){
    Particle *q=&g->particles[g->particle_cursor++%MAX_PARTICLES];*q=(Particle){p,vel,life,life,size,color};
}
static void toast(Game *g,const char *s,float seconds){SDL_strlcpy(g->toast,s,sizeof g->toast);g->toast_time=seconds;}

static void update_car(Game *g,int index,Controls in,float dt){
    Track *t=&g->tracks[g->selected];Car *c=&g->cars[index];c->prev=c->pos;c->prev_yaw=c->yaw;
    if(c->finished||c->dnf)return;
    c->ramp_cooldown=fmaxf(0,c->ramp_cooldown-dt);
    for(int j=0;j<t->can_count;j++)c->can_cooldown[j]=fmaxf(0,c->can_cooldown[j]-dt);
    if(in.reset){game_reset_car(g,index);return;}
    float lane;Vec3 center;track_nearest(t,c->pos,&c->nearest,&center,&lane);
    float off=fmaxf(0,fabsf(lane)-t->points[c->nearest].half);
    float grip=g->selected==2?5.8f:g->selected==1?7.4f:9.2f;
    if(in.drift)grip*=.43f;
    Vec3 forward=v3(sinf(c->yaw),0,cosf(c->yaw)),right=v3(forward.z,0,-forward.x);
    float longitudinal=vdot(c->velocity,forward),side=vdot(c->velocity,right);
    float throttle=c->fuel>0?in.throttle:0;
    float accel=throttle*(off>.5f?9.f:14.2f)-in.brake*24.f-.70f-longitudinal*longitudinal*.012f-off*longitudinal*.40f;
    if(!c->grounded)accel=-.8f;
    longitudinal=fmaxf(0,longitudinal+accel*dt);side*=expf(-grip*dt*(c->grounded?1:.15f));
    float steer_rate=(.38f+fminf(longitudinal,23)*.060f)*(in.drift?1.3f:1);
    c->yaw+=in.steer*steer_rate*clampf(longitudinal*.35f,0,1)*dt*(c->grounded?1:.23f);
    c->velocity=vadd(vmul(forward,longitudinal),vmul(right,side));c->speed=vlen(c->velocity);
    c->pos=vadd(c->pos,vmul(c->velocity,dt));
    bool was_ramp=false,is_ramp=false;
    float old_ground=track_height(t,c->nearest,c->prev,lane,&was_ramp);
    track_nearest(t,c->pos,&c->nearest,&center,&lane);
    float ground=track_height(t,c->nearest,c->pos,lane,&is_ramp);
    if(fabsf(lane)>t->points[c->nearest].half+.6f)ground=fmaxf(ground,terrain_surface(g->selected,c->pos.x,c->pos.z));
    // Leave the wedge's lip with velocity derived from its physical slope.
    if(c->grounded&&was_ramp&&!is_ramp&&c->speed>9&&c->ramp_cooldown<=0){
        c->grounded=false;c->vy=c->speed*.265f+1.3f;c->pos.y=old_ground;c->jumps++;c->ramp_cooldown=1.5f;c->air_time=0;
        if(index==0){toast(g,"TAKE FLIGHT!",1.2f);g->sound_event=4;}
    }
    if(!c->grounded){
        c->air_time+=dt;c->vy-=20*dt;c->pos.y+=c->vy*dt;c->pitch=lerpf(c->pitch,-atan2f(c->vy,fmaxf(8,c->speed)),dt*4);
        if(c->pos.y<=ground&&c->vy<0){
            c->pos.y=ground;c->grounded=true;c->vy=0;c->speed*=.97f;
            for(int j=0;j<10;j++)particle(g,c->pos,v3((random01(g)-.5f)*4,1+random01(g)*2,(random01(g)-.5f)*4),.55f,.3f,t->sky);
            if(index==0){g->shake=.23f;g->sound_event=5;}
        }
    }else{
        c->pos.y=ground;Vec3 d;track_at(t,c->last_s+1,0,&d);c->pitch=lerpf(c->pitch,-asinf(clampf(d.y,-.6f,.6f)),dt*14);
    }
    c->roll=lerpf(c->roll,-in.steer*c->speed*.004f,dt*8);
    float maxlane=t->points[c->nearest].half+4;
    if(fabsf(lane)>maxlane){
        Vec3 dir=v3(t->points[c->nearest].dx,0,t->points[c->nearest].dz);float sign=lane>0?1:-1;
        c->pos.x=center.x+dir.z*maxlane*sign;c->pos.z=center.z-dir.x*maxlane*sign;c->velocity=vmul(c->velocity,.95f);
        if(index==0)g->shake=fmaxf(g->shake,.08f);
    }
    float s=track_nearest(t,c->pos,&c->nearest,NULL,NULL),delta=s-c->last_s;
    if(delta>t->length*.5f)delta-=t->length;if(delta<-t->length*.5f)delta+=t->length;
    if(fabsf(delta)<12)c->progress+=delta;c->last_s=s;
    // Sequential quarter gates make reversing over the finish line harmless.
    while(c->progress>=(c->checkpoints+1)*t->length*.25f&&c->checkpoints<RACE_LAPS*4)c->checkpoints++;
    int lap=(int)(fmaxf(0,c->progress)/t->length)+1;
    if(lap>c->lap){
        c->lap_time=g->time-c->lap_start;c->lap_start=g->time;if(c->best_lap==0||c->lap_time<c->best_lap)c->best_lap=c->lap_time;
        if(index==0&&lap<=RACE_LAPS){g->last_lap=c->lap_time;toast(g,"FINAL LAP",2);g->sound_event=3;}
    }c->lap=lap;
    if(c->progress>=t->length*RACE_LAPS&&c->checkpoints>=RACE_LAPS*4){c->finished=true;c->finish_time=g->time;if(index==0)game_results(g);return;}
    c->fuel=fmaxf(0,c->fuel-(.65f+throttle*.45f+c->speed*.038f)*dt);
    for(int j=0;j<t->can_count;j++)if(c->can_cooldown[j]<=0){
        const Can *can=&t->cans[j];Vec3 cp=track_at(t,t->points[(int)can->index].s,can->lane,NULL);
        float dx=c->pos.x-cp.x,dz=c->pos.z-cp.z;
        if(dx*dx+dz*dz<2.35f*2.35f&&fabsf(c->pos.y-cp.y)<2.8f){
            c->can_cooldown[j]=22;c->fuel=fminf(100,c->fuel+26);c->collected++;
            if(index==0){toast(g,"+26 FUEL",1.3f);g->sound_event=2;
                for(int k=0;k<20;k++)particle(g,vadd(cp,v3(0,1,0)),v3((random01(g)-.5f)*5,2+random01(g)*4,(random01(g)-.5f)*5),.8f,.16f,rgb(.51,.95,.62));}
        }
    }
    if(c->fuel<=0){c->empty_time+=dt;if(index==0&&c->empty_time<.03f){toast(g,"OUT OF FUEL",3);g->sound_event=6;}if(c->speed<.5f&&c->empty_time>2){c->dnf=true;if(index==0)game_results(g);}}
    if(c->speed<1&&in.throttle>.5f&&c->fuel>0)c->stuck_time+=dt;else c->stuck_time=0;
    if(index>0&&c->stuck_time>3)game_reset_car(g,index);
    float dust_rate=(off>.4f||fabsf(side)>2)?24.f:g->selected==0?5.f:12.f;
    if(c->grounded&&c->speed>6&&random01(g)<dt*dust_rate){
        Color color=g->selected==2?rgb(.91,.95,.96):g->selected==1?rgb(.80,.70,.48):rgb(.58,.57,.38);
        particle(g,vadd(c->pos,vmul(forward,-1.2f)),vadd(vmul(forward,-1.4f),v3((random01(g)-.5f)*2,.8f,(random01(g)-.5f)*2)),.55f,.09f+fabsf(side)*.03f,color);
    }
}

void game_results(Game *g){
    if(g->screen==SCREEN_RESULTS)return;
    g->screen=SCREEN_RESULTS;Car *p=&g->cars[0];g->finish_rank=1;
    for(int j=1;j<CAR_COUNT;j++)if(g->cars[j].finished&&(!p->finished||g->cars[j].finish_time<=p->finish_time))g->finish_rank++;
    if(p->dnf)g->finish_rank=8;
    if(p->finished){
        float *best=&g->profile.best[g->selected];if(*best==0||p->finish_time<*best)*best=p->finish_time;
        int medal=g->finish_rank<=3?4-g->finish_rank:0;if(medal>g->profile.medals[g->selected])g->profile.medals[g->selected]=medal;
        if(!g->test_mode)profile_save(&g->profile);
    }g->sound_event=7;
}

void game_tick(Game *g,Controls controls,float dt,bool autopilot){
    g->clock+=dt;g->toast_time=fmaxf(0,g->toast_time-dt);g->shake=fmaxf(0,g->shake-dt);
    for(int j=0;j<MAX_PARTICLES;j++){Particle *p=&g->particles[j];if(p->life>0){p->life-=dt;p->pos=vadd(p->pos,vmul(p->vel,dt));p->vel.y-=2*dt;}}
    if(g->screen==SCREEN_COUNTDOWN){
        int last=(int)ceilf(g->countdown);g->countdown-=dt;
        if((int)ceilf(g->countdown)!=last)g->sound_event=g->countdown<=0?3:1;
        if(g->countdown<=0){g->screen=SCREEN_RACE;toast(g,"GO!",.9f);}return;
    }
    if(g->screen==SCREEN_RESULTS){
        bool remaining=false;for(int i=1;i<CAR_COUNT;i++)if(!g->cars[i].finished&&!g->cars[i].dnf)remaining=true;
        if(remaining){g->time+=dt;for(int i=1;i<CAR_COUNT;i++){Controls ai;game_ai(g,i,&ai);update_car(g,i,ai,dt);if(g->time>220&&!g->cars[i].finished)g->cars[i].dnf=true;}}
        return;
    }
    if(g->screen!=SCREEN_RACE)return;
    g->time+=dt;
    for(int i=0;i<CAR_COUNT;i++){
        Controls in=controls;if(i>0||autopilot)game_ai(g,i,&in);update_car(g,i,in,dt);
    }
    for(int i=0;i<CAR_COUNT;i++)for(int j=i+1;j<CAR_COUNT;j++){
        Car *a=&g->cars[i],*b=&g->cars[j];if(a->finished||b->finished||a->dnf||b->dnf||fabsf(a->pos.y-b->pos.y)>1.5f)continue;
        // Two overlapping tire-width circles form an oriented vehicle capsule.
        // This prevents a car's hood from passing through another car's trunk.
        Vec3 af=v3(sinf(a->yaw)*.72f,0,cosf(a->yaw)*.72f),bf=v3(sinf(b->yaw)*.72f,0,cosf(b->yaw)*.72f);
        Vec3 d=vsub(a->pos,b->pos);d.y=0;float l=100;
        for(int aa=-1;aa<=1;aa+=2)for(int bb=-1;bb<=1;bb+=2){Vec3 diff=vsub(vadd(a->pos,vmul(af,(float)aa)),vadd(b->pos,vmul(bf,(float)bb)));diff.y=0;float len=vlen(diff);if(len<l){l=len;d=diff;}}
        if(l<1.65f&&l>.0001f){
            Vec3 n=vmul(d,1/l);float push=(1.65f-l)*.51f;a->pos=vadd(a->pos,vmul(n,push));b->pos=vsub(b->pos,vmul(n,push));
            float speed=vdot(vsub(a->velocity,b->velocity),n);if(speed<0){a->velocity=vsub(a->velocity,vmul(n,speed*.58f));b->velocity=vadd(b->velocity,vmul(n,speed*.58f));}
            if(i==0&&speed<-2)g->shake=.12f;
        }
    }
    g->rank=1;for(int j=1;j<CAR_COUNT;j++)if(g->cars[j].progress>g->cars[0].progress)g->rank++;
    Car *p=&g->cars[0];TrackPoint tp=g->tracks[g->selected].points[p->nearest];
    if(p->speed>3&&(p->velocity.x*tp.dx+p->velocity.z*tp.dz)<-1)g->wrong_way+=dt;else g->wrong_way=0;
}

static int failures=0;
#define CHECK(c,m) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",m);failures++;}}while(0)
int game_tests(void){
    Game *g=calloc(1,sizeof *g);if(!g||!game_load(g)){fprintf(stderr,"Cannot load test assets\n");free(g);return 1;}g->test_mode=true;
    for(int k=0;k<3;k++){
        g->selected=k;Track *t=&g->tracks[k];CHECK(t->length>600,"long track");CHECK(t->elevation>6,"genuine elevation");CHECK(t->ramp_count==3,"three ramps");
        for(int i=0;i<t->count;i+=13){int idx=-1;float lane;Vec3 cp;float s=track_nearest(t,t->points[i].p,&idx,&cp,&lane);CHECK(vlen(vsub(cp,t->points[i].p))<.01f,"track projection round trip");CHECK(fabsf(s-t->points[i].s)<.01f,"track distance round trip");}
        for(int difficulty=0;difficulty<3;difficulty++){
            g->profile.difficulty=difficulty;game_start(g);int steps=0;
            while(g->screen!=SCREEN_RESULTS&&steps++<120*220)game_tick(g,(Controls){0},FIXED_DT,true);
            printf("%s difficulty=%d: %.2fs, fuel=%0.1f, cans=%d, jumps=%d, rank=%d, finished=%d\n",t->id,difficulty,g->time,g->cars[0].fuel,g->cars[0].collected,g->cars[0].jumps,g->finish_rank,g->cars[0].finished);
            CHECK(g->cars[0].finished,"AI-driven full race finishes");CHECK(g->cars[0].collected>=4,"fuel pickup route works");CHECK(g->cars[0].jumps>=3,"physical jumps work");CHECK(isfinite(g->cars[0].pos.x),"finite simulation");
            int rivals=0;for(int i=1;i<8;i++){CHECK(!g->cars[i].dnf,"AI manages fuel");if(g->cars[i].progress>t->length*1.4f)rivals++;}CHECK(rivals>=5,"competitive AI field");
        }
    }
    g->selected=0;game_start(g);g->screen=SCREEN_RACE;g->cars[0].fuel=0;
    for(int i=0;i<120*5;i++)game_tick(g,(Controls){.throttle=1},FIXED_DT,false);
    CHECK(g->cars[0].dnf&&g->screen==SCREEN_RESULTS,"fuel exhaustion resolves the race");
    game_start(g);g->screen=SCREEN_PAUSE;Vec3 p=g->cars[0].pos;float fuel=g->cars[0].fuel;
    for(int i=0;i<120;i++)game_tick(g,(Controls){.throttle=1},FIXED_DT,false);
    CHECK(g->time==0&&vlen(vsub(p,g->cars[0].pos))==0&&g->cars[0].fuel==fuel,"pause freezes gameplay");
    game_start(g);g->screen=SCREEN_RACE;g->cars[0].progress=-2;g->cars[0].checkpoints=0;game_reset_car(g,0);
    CHECK(!g->cars[0].finished&&g->cars[0].checkpoints==0,"reset does not award checkpoints");
    game_start(g);g->screen=SCREEN_RACE;g->cars[0].pos=track_at(&g->tracks[0],-10,8,NULL);g->cars[0].nearest=-1;g->cars[0].last_s=track_nearest(&g->tracks[0],g->cars[0].pos,&g->cars[0].nearest,NULL,NULL);
    for(int i=0;i<120;i++)game_tick(g,(Controls){.throttle=1},FIXED_DT,false);
    CHECK(g->cars[0].speed>1,"a stopped car can accelerate off the road");
    printf("Simulation tests: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);free(g);return failures?1:0;
}
