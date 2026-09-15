#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failures++; } } while (0)
// Synthetic flat ground makes momentum, settling and stack tests independent of art.
static void flat(Game *g) {
    Track *t=&g->tracks[0];
    t->surface_count=2;
    t->surface[0]=(SurfaceTriangle){v3(-128,0,-128),v3(128,0,-128),v3(128,0,128)};
    t->surface[1]=(SurfaceTriangle){v3(-128,0,-128),v3(128,0,128),v3(-128,0,128)};
    for (int i=0;i<SURFACE_GRID*SURFACE_GRID;i++) {
        t->surface_cells[i]=i*2; t->surface_refs[i*2]=0; t->surface_refs[i*2+1]=1;
    }
    t->surface_cells[SURFACE_GRID*SURFACE_GRID]=SURFACE_GRID*SURFACE_GRID*2;
    for (int i=0;i<CAR_COUNT;i++) g->cars[i].dnf=true;
}
static TrackProp body(PropKind kind, Vec3 p) {
    float mass=kind==PROP_TIRE ? 11 : 3;
    return (TrackProp){.kind=kind,.pos=p,.prev=p,.anchor=vsub(p,v3(0,.60f,0)),.rotation=identity(),.prev_rotation=identity(),
        .inv_mass=1/mass,.inv_inertia=1/(mass*(kind==PROP_POST ? .56f : kind==PROP_TIRE ? .23f : .16f))};
}
int main(void) {
    Game *g=calloc(1,sizeof *g); if (!g) return 1;
    flat(g);
    g->prop_count=3;
    for (int i=0;i<3;i++) g->props[i]=body(PROP_TIRE,v3(0,.20f+i*.40f,0));
    for (int tick=0;tick<360;tick++) props_tick(g,FIXED_DT);
    for (int i=0;i<3;i++) {
        CHECK(fabsf(g->props[i].pos.y-(.20f+i*.40f))<.08f);
        CHECK(hypotf(g->props[i].pos.x,g->props[i].pos.z)<.1f);
    }
    // A real moving car strikes the bottom of a stack, transfers momentum,
    // loses speed, and leaves upper tires without their support.
    Car *car=&g->cars[0];
    *car=(Car){.pos={0,0,-2},.velocity={0,0,16},.grounded=true,.reset_protection=3};
    for (int i=0;i<20;i++) { car->pos=vadd(car->pos,vmul(car->velocity,FIXED_DT)); props_tick(g,FIXED_DT); }
    CHECK(car->velocity.z==16 && !car->tumbling);
    for (int i=0;i<3;i++) CHECK(hypotf(g->props[i].pos.x,g->props[i].pos.z)<.1f);
    *car=(Car){.pos={0,0,-2},.velocity={0,0,16},.grounded=true};
    for (int i=0;i<20;i++) { car->pos=vadd(car->pos,vmul(car->velocity,FIXED_DT)); props_tick(g,FIXED_DT); }
    CHECK(g->props[0].velocity.z>1);
    CHECK(car->velocity.z<16);
    car->dnf=true;
    for (int i=0;i<1200;i++) props_tick(g,FIXED_DT);
    CHECK(g->props[2].pos.y<.7f);
    for (int i=0;i<3;i++) { CHECK(isfinite(g->props[i].pos.y)); CHECK(g->props[i].pos.y>.1f); CHECK(vlen(g->props[i].velocity)<.3f); }
    g->prop_count=1;
    g->props[0]=body(PROP_CONE,v3(0,.29f,0));
    *car=(Car){.pos={.25f,0,-2},.velocity={0,0,12},.grounded=true};
    float min_up=1;
    for (int i=0;i<25;i++) { car->pos=vadd(car->pos,vmul(car->velocity,FIXED_DT)); props_tick(g,FIXED_DT); min_up=fminf(min_up,g->props[0].rotation.m[5]); }
    CHECK(vlen(g->props[0].omega)>.5f);
    CHECK(g->props[0].pos.z>.5f);
    car->dnf=true;
    for (int i=0;i<1200;i++) { props_tick(g,FIXED_DT); min_up=fminf(min_up,g->props[0].rotation.m[5]); }
    CHECK(min_up<.99f); // A low bumper strike can rock a weighted cone upright.
    CHECK(vlen(g->props[0].velocity)<.3f);
    CHECK(g->props[0].pos.y>.05f);
    g->props[0]=body(PROP_CONE,v3(0,.8f,0));
    g->props[0].velocity=v3(0,0,3);
    g->props[0].omega=v3(8,0,0);
    min_up=1;
    for (int i=0;i<1200;i++) { props_tick(g,FIXED_DT); min_up=fminf(min_up,g->props[0].rotation.m[5]); }
    CHECK(min_up<0);
    CHECK(vlen(g->props[0].velocity)<.3f);
    // A flexible panel must respond even when the car misses both stakes.
    g->prop_count=2; g->fence_count=1;
    for (int i=0;i<2;i++) {
        g->props[i]=body(PROP_POST,v3(i ? 2 : -2,.665f,0));
        g->props[i].asleep=true;
    }
    FencePanel *f=&g->fences[0];
    *f=(FencePanel){.posts={0,1},.width=4};
    for (int row=0;row<FENCE_ROWS;row++) for (int col=0;col<FENCE_COLS;col++) {
        Vec3 p=v3(-2+4.f*col/(FENCE_COLS-1),.305f+row*.4f,0);
        f->nodes[row*FENCE_COLS+col]=(FenceNode){p,p,p};
    }
    *car=(Car){.pos={0,0,-3},.velocity={0,0,18},.grounded=true};
    float movement=0;
    for (int tick=0;tick<60;tick++) {
        car->pos=vadd(car->pos,vmul(car->velocity,FIXED_DT));
        props_tick(g,FIXED_DT);
        movement=fmaxf(movement,fabsf(f->nodes[FENCE_COLS+3].pos.z));
        for (int i=0;i<2;i++) {
            Vec3 travel=vsub(g->props[i].pos,g->props[i].anchor);
            CHECK(hypotf(travel.x,travel.z)<.7f);
            CHECK(g->props[i].pos.y<.75f);
        }
    }
    CHECK(movement>.4f);
    CHECK(car->velocity.z<18 && car->velocity.z>10);
    CHECK(g->props[0].pos.z>.1f || g->props[1].pos.z>.1f);
    car->dnf=true;
    for (int tick=0;tick<600;tick++) props_tick(g,FIXED_DT);
    CHECK(g->props[0].rotation.m[5]<.8f || g->props[1].rotation.m[5]<.8f);
    for (int n=0;n<FENCE_NODES;n++) {
        CHECK(isfinite(f->nodes[n].pos.x));
        CHECK(f->nodes[n].pos.y>-.1f && f->nodes[n].pos.y<3);
    }
    // Direct fast impacts, repeated passes and oblique hits must fold stakes
    // locally, never launch them (including when the car hits the foot).
    g->fence_count=0; g->prop_count=1;
    for (int trial=0;trial<6;trial++) {
        g->props[0]=body(PROP_POST,v3(0,.665f,0));
        float speed=trial%3==0 ? 8 : trial%3==1 ? 20 : 40;
        float yaw=trial<3 ? 0 : .65f;
        Vec3 direction=v3(sinf(yaw),0,cosf(yaw));
        *car=(Car){.pos=vmul(direction,-3),.velocity=vmul(direction,speed),.yaw=yaw,.grounded=true};
        float min_up=1;
        for (int tick=0;tick<720;tick++) {
            if (tick==120) car->pos=vmul(direction,-3);
            if (tick==240) car->dnf=true;
            car->pos=vadd(car->pos,vmul(car->velocity,FIXED_DT));
            props_tick(g,FIXED_DT);
            TrackProp *p=&g->props[0];
            CHECK(hypotf(p->pos.x,p->pos.z)<.7f);
            CHECK(p->pos.y<.75f && p->pos.y>-.1f);
            CHECK(isfinite(p->omega.x));
            CHECK(p->anchor.y+1.2f*p->rotation.m[5]>=.06f);
            min_up=fminf(min_up,p->rotation.m[5]);
        }
        CHECK(min_up<.5f);
        CHECK(vlen(g->props[0].omega)<.3f);
    }
    // Check placement and reset against every shipped track.
    CHECK(game_load(g));
    for (int track=0;track<TRACK_COUNT;track++) {
        g->selected=track; props_reset(g);
        printf("Track %d: %d fence panels\n",track,g->fence_count);
        CHECK(g->fence_count>=4 && g->fence_count<=MAX_FENCE_PANELS);
        CHECK(g->prop_count<=MAX_TRACK_PROPS);
        int tires=0;
        for (int i=0;i<g->prop_count;i++) if (g->props[i].kind==PROP_TIRE) {
            tires++;
            int nearest=-1; float lane,water; Vec3 p=g->props[i].pos;
            track_nearest(&g->tracks[track],p,&nearest,NULL,&lane);
            CHECK(fabsf(lane)>g->tracks[track].points[nearest].half+.6f);
            CHECK(!track_water(&g->tracks[track],p,&water) ||
                  water<=track_height(&g->tracks[track],p,NULL));
        }
        CHECK(tires==90);
        for (int i=0;i<g->prop_count;i++) if (g->props[i].kind==PROP_POST) {
            int nearest=-1; float lane,water; Vec3 p=g->props[i].pos;
            track_nearest(&g->tracks[track],p,&nearest,NULL,&lane);
            CHECK(fabsf(lane)>g->tracks[track].points[nearest].half+2);
            CHECK(!track_water(&g->tracks[track],p,&water) || water<p.y-.6f);
        }
        Vec3 initial=g->fences[0].nodes[3].pos;
        g->fences[0].nodes[3].pos.y+=2;
        props_reset(g);
        CHECK(vlen(vsub(initial,g->fences[0].nodes[3].pos))<.0001f);
    }
    free(g);
    printf("Track props: %d failures\n",failures);
    return failures ? 1 : 0;
}
