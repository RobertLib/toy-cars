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
    return (TrackProp){.kind=kind,.pos=p,.prev=p,.rotation=identity(),.prev_rotation=identity(),
        .inv_mass=1/mass,.inv_inertia=1/(mass*(kind==PROP_TIRE ? .23f : .16f))};
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
    free(g);
    printf("Track props: %d failures\n",failures);
    return failures ? 1 : 0;
}
