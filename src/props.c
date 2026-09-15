#include "game.h"
#include <string.h>

// Compound sphere hulls leave the tire hole open and allow cones to tip.
static Vec3 rotated(Mat4 m, Vec3 p) {
    return v3(m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z,
              m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z,
              m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z);
}
static int hull(const TrackProp *p, Vec3 *offset, float *radius) {
    int count = p->kind == PROP_TIRE ? 12 : 6;
    for (int i = 0; i < count; i++) {
        Vec3 local;
        if (p->kind == PROP_TIRE) {
            float a = i * (2*PI/12);
            local = v3(.43f*cosf(a), 0, .43f*sinf(a));
            radius[i] = .20f;
        } else if (i < 4) {
            local = v3(i&1 ? .27f : -.27f, -.20f, i&2 ? .27f : -.27f);
            radius[i] = .09f;
        } else {
            local = v3(0, i == 4 ? .05f : .47f, 0);
            radius[i] = i == 4 ? .23f : .09f;
        }
        offset[i] = rotated(p->rotation, local);
    }
    return count;
}
static void impulse(TrackProp *p, Vec3 r, Vec3 j) {
    p->velocity = vadd(p->velocity, vmul(j, p->inv_mass));
    p->omega = vadd(p->omega, vmul(vcross(r,j), p->inv_inertia));
}
static float effective(const TrackProp *p, Vec3 r, Vec3 n) {
    Vec3 cross = vcross(r,n);
    return p->inv_mass + p->inv_inertia*vdot(cross,cross);
}
static Vec3 contact_velocity(const TrackProp *p, Vec3 r) {
    return vadd(p->velocity, vcross(p->omega,r));
}
static void contact(TrackProp *a, TrackProp *b, Vec3 ra, Vec3 rb, Vec3 n, float depth) {
    a->asleep = false;
    if (b) b->asleep = false;
    float inverse = a->inv_mass + (b ? b->inv_mass : 0);
    float correction = fmaxf(0, depth-.002f)*.32f/inverse;
    a->pos = vadd(a->pos, vmul(n, correction*a->inv_mass));
    if (b) b->pos = vsub(b->pos, vmul(n, correction*b->inv_mass));
    Vec3 relative = vsub(contact_velocity(a,ra), b ? contact_velocity(b,rb) : v3(0,0,0));
    float speed = vdot(relative,n);
    if (speed >= 0) return;
    float denom = effective(a,ra,n) + (b ? effective(b,rb,n) : 0);
    float bounce = speed < -1.5f ? (a->kind == PROP_TIRE ? .28f : .12f) : 0;
    float j = -(1+bounce)*speed/denom;
    impulse(a,ra,vmul(n,j));
    if (b) impulse(b,rb,vmul(n,-j));
    relative = vsub(contact_velocity(a,ra), b ? contact_velocity(b,rb) : v3(0,0,0));
    Vec3 tangent = vsub(relative,vmul(n,vdot(relative,n)));
    float slip = vlen(tangent);
    if (slip < .00001f) return;
    tangent = vmul(tangent,1/slip);
    float friction = fminf(j*.7f, slip/(effective(a,ra,tangent)+(b ? effective(b,rb,tangent) : 0)));
    impulse(a,ra,vmul(tangent,-friction));
    if (b) impulse(b,rb,vmul(tangent,friction));
}
static void add_prop(Game *g, PropKind kind, Vec3 pos) {
    if (g->prop_count == MAX_TRACK_PROPS) return;
    TrackProp *p = &g->props[g->prop_count++];
    memset(p,0,sizeof *p);
    p->kind = kind;
    p->asleep = true;
    p->pos = p->prev = pos;
    p->rotation = p->prev_rotation = identity();
    p->inv_mass = kind == PROP_CONE ? 1.f/3 : 1.f/11;
    p->inv_inertia = p->inv_mass/(kind == PROP_CONE ? .16f : .23f);
}
void props_reset(Game *g) {
    const Track *t = &g->tracks[g->selected];
    g->prop_count = 0;
    // A repair patch on the shoulder, with a clear racing line beside it.
    int index = t->count/7;
    float s = t->points[index].s, lane = t->points[index].half-.65f;
    Vec3 dir;
    g->roadwork = track_at(t,s,lane,&dir);
    g->roadwork.y = track_height(t,g->roadwork,NULL)+.025f;
    g->roadwork_yaw = atan2f(dir.x,dir.z);
    for (int i = 0; i < 6; i++) {
        Vec3 p = track_at(t,s+(i/2-1)*2.1f,lane+(i%2 ? .85f : -.85f),NULL);
        p.y = track_height(t,p,NULL)+.29f;
        add_prop(g,PROP_CONE,p);
    }
    // Pick the strongest bend in each half, away from jumps and water.
    for (int section = 0; section < 2; section++) {
        float best = -1, bend = 0;
        int chosen = -1;
        for (int i = t->count*(section*4+1)/8; i < t->count*(section*4+4)/8; i+=4) {
            Vec3 a,b;
            float distance = t->points[i].s;
            track_at(t,distance-8,0,&a); track_at(t,distance+8,0,&b);
            float turn = a.z*b.x-a.x*b.z;
            bool safe = true;
            for (int j=0;j<t->ramp_count;j++)
                if (fabsf(distance-t->points[(int)t->ramps[j].index].s)<25) safe=false;
            float side = turn > 0 ? -1 : 1;
            Vec3 p = track_at(t,distance,side*(t->points[i].half+.85f),NULL);
            float level;
            if (track_water(t,p,&level) && level>track_height(t,p,NULL)) safe=false;
            if (safe && fabsf(turn)>best) { best=fabsf(turn); chosen=i; bend=side; }
        }
        if (chosen < 0) continue;
        for (int stack=0;stack<3;stack++) {
            Vec3 p = track_at(t,t->points[chosen].s+(stack-1)*1.35f,
                             bend*(t->points[chosen].half+.85f),NULL);
            // Seat the whole ring above the highest supporting ground sample.
            float ground = track_height(t,p,NULL);
            for (int j=0;j<12;j++) {
                float a=j*2*PI/12;
                ground=fmaxf(ground,track_height(t,vadd(p,v3(.43f*cosf(a),0,.43f*sinf(a))),NULL));
            }
            for (int level=0;level<3;level++) {
                p.y=ground+.20f+level*.40f;
                add_prop(g,PROP_TIRE,p);
            }
        }
    }
}
void props_tick(Game *g, float dt) {
    const Track *t=&g->tracks[g->selected];
    for (int i=0;i<g->prop_count;i++) {
        TrackProp *p=&g->props[i];
        p->prev=p->pos; p->prev_rotation=p->rotation;
        for (int j=0;j<CAR_COUNT;j++) {
            Car *c=&g->cars[j];
            if (!c->dnf && vlen(vsub(p->pos,c->pos))<3.2f && vlen(c->velocity)>.15f) {
                p->asleep=false; p->quiet_time=0;
            }
        }
    }
    int steps=(int)ceilf(dt/FIXED_DT);
    if (steps < 1) return;
    float h=dt/steps;
    for (int step=0;step<steps;step++) {
        for (int i=0;i<g->prop_count;i++) {
            TrackProp *p=&g->props[i];
            if (p->asleep) continue;
            p->velocity.y-=9.81f*h;
            p->velocity=vmul(p->velocity,expf(-.08f*h));
            p->omega=vmul(p->omega,expf(-.18f*h));
            p->pos=vadd(p->pos,vmul(p->velocity,h));
            // Integrate the world angular velocity, then re-orthonormalize.
            Vec3 x=rotated(p->rotation,v3(1,0,0)), y=rotated(p->rotation,v3(0,1,0));
            x=vnorm(vadd(x,vmul(vcross(p->omega,x),h)));
            y=vadd(y,vmul(vcross(p->omega,y),h));
            Vec3 z=vnorm(vcross(x,y)); y=vcross(z,x);
            memcpy(p->rotation.m, &x, sizeof x);
            memcpy(p->rotation.m+4, &y, sizeof y);
            memcpy(p->rotation.m+8, &z, sizeof z);
        }
        for (int iteration=0;iteration<5;iteration++) {
            for (int i=0;i<g->prop_count;i++) {
                TrackProp *a=&g->props[i]; Vec3 ra[12]; float ar[12]; int na=hull(a,ra,ar);
                if (!a->asleep) for (int k=0;k<na;k++) {
                    Vec3 q=vadd(a->pos,ra[k]); float ground=track_height(t,q,NULL);
                    float depth=ground+ar[k]-q.y;
                    if (depth>0) {
                        float gx=track_height(t,vadd(q,v3(.04f,0,0)),NULL);
                        float gz=track_height(t,vadd(q,v3(0,0,.04f)),NULL);
                        Vec3 n=vnorm(v3(clampf((ground-gx)/.04f,-2,2),1,clampf((ground-gz)/.04f,-2,2)));
                        contact(a,NULL,vsub(ra[k],vmul(n,ar[k])),v3(0,0,0),n,depth);
                    }
                }
                for (int j=i+1;j<g->prop_count;j++) {
                    TrackProp *b=&g->props[j]; if (a->asleep && b->asleep) continue;
                    if (vlen(vsub(a->pos,b->pos))>1.5f) continue;
                    Vec3 rb[12]; float br[12]; int nb=hull(b,rb,br);
                    for (int k=0;k<na;k++) for (int l=0;l<nb;l++) {
                        Vec3 d=vsub(vadd(a->pos,ra[k]),vadd(b->pos,rb[l])); float len=vlen(d);
                        if (len<ar[k]+br[l] && len>.00001f) {
                            Vec3 n=vmul(d,1/len);
                            contact(a,b,vsub(ra[k],vmul(n,ar[k])),vadd(rb[l],vmul(n,br[l])),n,ar[k]+br[l]-len);
                        }
                    }
                }
                for (int j=0;j<CAR_COUNT;j++) {
                    Car *c=&g->cars[j];
                    if (c->dnf || vlen(vsub(a->pos,c->pos))>3) continue;
                    TrackProp car={.pos=c->pos,.velocity=c->velocity,.inv_mass=1.f/180};
                    car.velocity.y=c->vy;
                    for (int end=-1;end<=1;end+=2) {
                        Vec3 center=vadd(car.pos,v3(sinf(c->yaw)*end*.72f,.65f,cosf(c->yaw)*end*.72f));
                        for (int k=0;k<na;k++) {
                            Vec3 d=vsub(vadd(a->pos,ra[k]),center); float len=vlen(d);
                            if (len<ar[k]+.82f && len>.00001f) {
                                Vec3 n=vmul(d,1/len);
                                contact(a,&car,vsub(ra[k],vmul(n,ar[k])),v3(0,0,0),n,ar[k]+.82f-len);
                            }
                        }
                    }
                    c->pos.x=car.pos.x; c->pos.z=car.pos.z;
                    c->velocity.x=car.velocity.x; c->velocity.z=car.velocity.z;
                    if (!c->grounded) c->vy=car.velocity.y;
                }
            }
        }
    }
    for (int i=0;i<g->prop_count;i++) {
        TrackProp *p=&g->props[i];
        if (vlen(p->velocity)<.09f && vlen(p->omega)<.12f) p->quiet_time+=dt;
        else p->quiet_time=0;
        if (p->quiet_time>.8f) {
            p->asleep=true; p->velocity=p->omega=v3(0,0,0);
        }
    }
}
