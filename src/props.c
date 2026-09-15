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
        if (p->kind == PROP_POST) {
            local = v3(0, -.60f + i*.24f, 0);
            radius[i] = .065f;
        } else if (p->kind == PROP_TIRE) {
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
    if (p->kind == PROP_POST) {
        Vec3 arm=vadd(r,rotated(p->rotation,v3(0,.60f,0)));
        p->omega=vadd(p->omega,vmul(vcross(arm,j),p->inv_inertia));
        float spin=vlen(p->omega);
        if (spin>5) p->omega=vmul(p->omega,5/spin);
        return;
    }
    p->velocity = vadd(p->velocity, vmul(j, p->inv_mass));
    p->omega = vadd(p->omega, vmul(vcross(r,j), p->inv_inertia));
}
static float effective(const TrackProp *p, Vec3 r, Vec3 n) {
    if (p->kind == PROP_POST) {
        Vec3 arm=vadd(r,rotated(p->rotation,v3(0,.60f,0)));
        Vec3 cross=vcross(arm,n);
        return fmaxf(.02f,p->inv_inertia*vdot(cross,cross));
    }
    Vec3 cross = vcross(r,n);
    return p->inv_mass + p->inv_inertia*vdot(cross,cross);
}
static Vec3 contact_velocity(const TrackProp *p, Vec3 r) {
    if (p->kind==PROP_POST)
        return vcross(p->omega,vadd(r,rotated(p->rotation,v3(0,.60f,0))));
    return vadd(p->velocity, vcross(p->omega,r));
}
static void contact(TrackProp *a, TrackProp *b, Vec3 ra, Vec3 rb, Vec3 n, float depth) {
    a->asleep = false;
    if (b) b->asleep = false;
    float inverse = a->inv_mass + (b ? b->inv_mass : 0);
    float correction = fmaxf(0, depth-.002f)*.32f/inverse;
    if (a->kind!=PROP_POST) a->pos = vadd(a->pos, vmul(n, correction*a->inv_mass));
    if (b && b->kind!=PROP_POST) b->pos = vsub(b->pos, vmul(n, correction*b->inv_mass));
    Vec3 relative = vsub(contact_velocity(a,ra), b ? contact_velocity(b,rb) : v3(0,0,0));
    float speed = vdot(relative,n);
    if (speed >= 0) return;
    float denom = effective(a,ra,n) + (b ? effective(b,rb,n) : 0);
    float bounce = a->kind != PROP_POST && speed < -1.5f ? (a->kind == PROP_TIRE ? .28f : .12f) : 0;
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
    p->anchor = vsub(pos,v3(0,.60f,0));
    p->rotation = p->prev_rotation = identity();
    p->inv_mass = kind == PROP_TIRE ? 1.f/11 : 1.f/3;
    p->inv_inertia = p->inv_mass/(kind == PROP_POST ? .56f : kind == PROP_CONE ? .16f : .23f);
}
static void seat_post(TrackProp *p, const Track *t) {
    Vec3 up=rotated(p->rotation,v3(0,1,0));
    float min_y=up.y;
    for (int k=1;k<6;k++) {
        float length=k*.24f;
        Vec3 q=vadd(p->anchor,vmul(up,length));
        min_y=fmaxf(min_y,(track_height(t,q,NULL)+.065f-p->anchor.y)/length);
    }
    if (min_y>up.y+.00001f) {
        // Resolve terrain penetration by lifting the shaft around its fixed foot.
        float horizontal=hypotf(up.x,up.z);
        up.y=clampf(min_y,-1,1);
        float scale=horizontal>.00001f ? sqrtf(fmaxf(0,1-up.y*up.y))/horizontal : 0;
        up.x*=scale; up.z*=scale;
        Vec3 x=vnorm(vcross(up,rotated(p->rotation,v3(0,0,1))));
        Vec3 z=vcross(x,up);
        memcpy(p->rotation.m,&x,sizeof x);
        memcpy(p->rotation.m+4,&up,sizeof up);
        memcpy(p->rotation.m+8,&z,sizeof z);
        p->omega=vmul(p->omega,.45f);
    }
    Vec3 arm=rotated(p->rotation,v3(0,.60f,0));
    p->pos=vadd(p->anchor,arm);
    p->velocity=vcross(p->omega,arm);
}
static void fences_reset(Game *g);
static void fences_tick(Game *g, float h);

void props_reset(Game *g) {
    const Track *t = &g->tracks[g->selected];
    g->prop_count = 0;
    g->fence_count = 0;
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
    // Spread barriers around six sections, away from the grid, jumps and water.
    for (int section = 0; section < 6; section++) {
        float best = -1, bend = 0;
        int chosen = -1;
        for (int i = t->count*section/6; i < t->count*(section+1)/6; i+=4) {
            Vec3 a,b;
            float distance = t->points[i].s;
            if (distance < 22 || distance > t->length-22) continue;
            track_at(t,distance-8,0,&a); track_at(t,distance+8,0,&b);
            float turn = a.z*b.x-a.x*b.z;
            bool safe = true;
            for (int j=0;j<t->ramp_count;j++) {
                float d=fabsf(distance-t->points[(int)t->ramps[j].index].s);
                if (fminf(d,t->length-d)<25) safe=false;
            }
            for (int j=6;j<g->prop_count;j+=15) {
                int nearest=-1;
                float other=track_nearest(t,g->props[j].pos,&nearest,NULL,NULL);
                float d=fabsf(distance-other);
                if (fminf(d,t->length-d)<18) safe=false;
            }
            float side = turn > 0 ? -1 : 1;
            for (int stack=-2;stack<=2;stack++) {
                Vec3 p = track_at(t,distance+stack*1.35f,side*(t->points[i].half+.85f),NULL);
                int nearest=-1;
                float level,offset;
                track_nearest(t,p,&nearest,NULL,&offset);
                if (fabsf(offset)<t->points[nearest].half+.65f) safe=false;
                if (track_water(t,p,&level) && level>track_height(t,p,NULL)) safe=false;
            }
            if (safe && fabsf(turn)>best) { best=fabsf(turn); chosen=i; bend=side; }
        }
        if (chosen < 0) continue;
        for (int stack=0;stack<5;stack++) {
            Vec3 p = track_at(t,t->points[chosen].s+(stack-2)*1.35f,
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
    fences_reset(g);
}
void props_tick(Game *g, float dt) {
    const Track *t=&g->tracks[g->selected];
    for (int i=0;i<g->fence_count;i++)
        for (int n=0;n<FENCE_NODES;n++)
            g->fences[i].nodes[n].render_prev=g->fences[i].nodes[n].pos;
    for (int i=0;i<g->prop_count;i++) {
        TrackProp *p=&g->props[i];
        p->prev=p->pos; p->prev_rotation=p->rotation;
        for (int j=0;j<CAR_COUNT;j++) {
            Car *c=&g->cars[j];
            if (!c->dnf && c->reset_protection <= 0 && vlen(vsub(p->pos,c->pos))<3.2f && vlen(c->velocity)>.15f) {
                p->asleep=false; p->quiet_time=0;
            }
        }
    }
    int steps=(int)ceilf(dt/FIXED_DT);
    if (steps < 1) return;
    float h=dt/steps;
    for (int step=0;step<steps;step++) {
        fences_tick(g,h);
        for (int i=0;i<g->prop_count;i++) {
            TrackProp *p=&g->props[i];
            if (p->asleep) continue;
            if (p->kind == PROP_POST) {
                // Soil holds the foot while the stake folds over, dissipating energy.
                impulse(p,v3(0,0,0),v3(0,-9.81f*h/p->inv_mass,0));
                p->omega=vmul(p->omega,expf(-3.5f*h));
            } else p->velocity.y-=9.81f*h;
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
            if (p->kind==PROP_POST) seat_post(p,t);
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
                    if (c->dnf || c->reset_protection > 0 || vlen(vsub(a->pos,c->pos))>3) continue;
                    TrackProp car={.pos=c->pos,.velocity=c->velocity,.inv_mass=1.f/180};
                    car.velocity.y=c->vy;
                    for (int end=-1;end<=1;end+=2) {
                        Vec3 center=vadd(car.pos,v3(sinf(c->yaw)*end*.72f,.65f,cosf(c->yaw)*end*.72f));
                        for (int k=0;k<na;k++) {
                            Vec3 d=vsub(vadd(a->pos,ra[k]),center); float len=vlen(d);
                            if (len<ar[k]+.82f && len>.00001f) {
                                Vec3 n=vmul(d,1/len);
                                if (a->kind==PROP_POST) {
                                    // A yielding stake bends under the bumper; it must not
                                    // receive a full rigid-body launch impulse every iteration.
                                    if (iteration==0) {
                                        Vec3 push=v3(car.velocity.x,0,car.velocity.z);
                                        float speed=vlen(push);
                                        if (speed>.15f) {
                                            Vec3 j=vmul(push,fminf(speed,12)*h*18/speed);
                                            impulse(a,ra[k],j);
                                            a->asleep=false; a->quiet_time=0;
                                            car.velocity=vsub(car.velocity,vmul(j,car.inv_mass));
                                        }
                                    }
                                } else contact(a,&car,vsub(ra[k],vmul(n,ar[k])),v3(0,0,0),n,ar[k]+.82f-len);
                            }
                        }
                    }
                    car_impact(c, vsub(v3(car.velocity.x,0,car.velocity.z), c->velocity),
                               v3(0,-.30f,0));
                    c->pos.x=car.pos.x; c->pos.z=car.pos.z;
                    c->velocity.x=car.velocity.x; c->velocity.z=car.velocity.z;
                    if (!c->grounded) c->vy=car.velocity.y;
                }
                if (a->kind==PROP_POST && !a->asleep) seat_post(a,t);
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

// Four short exclusion-zone fences, with gaps between the selected bends.
static void fences_reset(Game *g) {
    const Track *t=&g->tracks[g->selected];
    if (t->count<16 || t->length<80) return;
    for (int section=0;section<4;section++) {
        float best=.10f, chosen=0, side=1;
        for (int i=t->count*section/4;i<t->count*(section+1)/4;i+=4) {
            float s=t->points[i].s;
            if (s<22 || s>t->length-22) continue;
            Vec3 a,b; track_at(t,s-7,0,&a); track_at(t,s+7,0,&b);
            float turn=a.z*b.x-a.x*b.z;
            bool safe=true;
            for (int j=0;j<t->ramp_count;j++) {
                float d=fabsf(s-t->points[(int)t->ramps[j].index].s);
                if (fminf(d,t->length-d)<32) safe=false;
            }
            for (int j=0;j<g->fence_count;j+=4) {
                int nearest=-1;
                float other=track_nearest(t,g->props[g->fences[j].posts[0]].pos,&nearest,NULL,NULL);
                float d=fabsf(s-other);
                if (fminf(d,t->length-d)<30) safe=false;
            }
            float lane=(turn>0 ? -1 : 1)*(t->points[i].half+3.2f);
            for (int k=-8;k<=8;k+=2) {
                Vec3 p=track_at(t,s+k,lane,NULL); float water;
                int nearest=-1; float offset;
                track_nearest(t,p,&nearest,NULL,&offset);
                if (fabsf(offset)<t->points[nearest].half+2 ||
                    (track_water(t,p,&water) && water>track_height(t,p,NULL))) safe=false;
            }
            if (safe && fabsf(turn)>best) { best=fabsf(turn); chosen=s; side=lane; }
        }
        if (!chosen) continue;
        int first=g->prop_count;
        for (int i=0;i<5;i++) {
            Vec3 p=track_at(t,chosen-8+i*4,side,NULL);
            p.y=track_height(t,p,NULL)+.665f;
            add_prop(g,PROP_POST,p);
        }
        for (int i=0;i<4;i++) {
            FencePanel *f=&g->fences[g->fence_count++];
            memset(f,0,sizeof *f);
            f->posts[0]=first+i; f->posts[1]=first+i+1;
            Vec3 a=g->props[first+i].pos,b=g->props[first+i+1].pos;
            f->width=vlen(vsub(a,b));
            for (int row=0;row<FENCE_ROWS;row++) for (int col=0;col<FENCE_COLS;col++) {
                Vec3 p=vlerp(a,b,(float)col/(FENCE_COLS-1));
                p.y+=-.36f+row*.40f;
                FenceNode *n=&f->nodes[row*FENCE_COLS+col];
                n->pos=n->previous=n->render_prev=p;
            }
        }
    }
}
static bool fence_fixed(int i) { return i%FENCE_COLS==0 || i%FENCE_COLS==FENCE_COLS-1; }
static void fence_link(FencePanel *f,int a,int b,float length) {
    Vec3 d=vsub(f->nodes[b].pos,f->nodes[a].pos); float distance=vlen(d);
    float wa=fence_fixed(a) ? 0 : 1, wb=fence_fixed(b) ? 0 : 1;
    if (distance<.00001f || wa+wb==0) return;
    Vec3 correction=vmul(d,(distance-length)/distance/(wa+wb)*.85f);
    f->nodes[a].pos=vadd(f->nodes[a].pos,vmul(correction,wa));
    f->nodes[b].pos=vsub(f->nodes[b].pos,vmul(correction,wb));
}
static void fences_tick(Game *g,float h) {
    const Track *t=&g->tracks[g->selected];
    for (int i=0;i<g->fence_count;i++) {
        FencePanel *f=&g->fences[i];
        unsigned nearby=0;
        Vec3 middle=vlerp(g->props[f->posts[0]].pos,g->props[f->posts[1]].pos,.5f);
        for (int j=0;j<CAR_COUNT;j++) {
            Car *c=&g->cars[j];
            if (!c->dnf && c->reset_protection <= 0 && vlen(vsub(c->pos,middle))<f->width*.5f+3) {
                nearby|=1u<<j;
                if (vlen(c->velocity)>.15f) f->awake_time=2;
            }
        }
        if (!g->props[f->posts[0]].asleep || !g->props[f->posts[1]].asleep) f->awake_time=2;
        if (f->awake_time<=0) continue;
        f->awake_time-=h;
        for (int n=0;n<FENCE_NODES;n++) {
            FenceNode *node=&f->nodes[n];
            if (fence_fixed(n)) {
                TrackProp *p=&g->props[f->posts[n%FENCE_COLS ? 1 : 0]];
                node->pos=vadd(p->pos,rotated(p->rotation,v3(0,-.36f+(n/FENCE_COLS)*.4f,0)));
                node->previous=node->pos;
            } else {
                Vec3 old=node->pos;
                node->pos=vadd(node->pos,vadd(vmul(vsub(node->pos,node->previous),expf(-3*h)),v3(0,-9.81f*h*h,0)));
                node->previous=old;
            }
        }
        for (int iteration=0;iteration<6;iteration++) {
            for (int row=0;row<FENCE_ROWS;row++) for (int col=0;col<FENCE_COLS;col++) {
                int n=row*FENCE_COLS+col;
                float dx=f->width/(FENCE_COLS-1);
                if (col+1<FENCE_COLS) fence_link(f,n,n+1,dx);
                if (row+1<FENCE_ROWS) {
                    fence_link(f,n,n+FENCE_COLS,.4f);
                    if (col+1<FENCE_COLS) {
                        float diagonal=sqrtf(dx*dx+.16f);
                        fence_link(f,n,n+FENCE_COLS+1,diagonal);
                        fence_link(f,n+1,n+FENCE_COLS,diagonal);
                    }
                }
            }
            for (int n=0;n<FENCE_NODES;n++) {
                if (fence_fixed(n)) continue;
                FenceNode *node=&f->nodes[n];
                for (int j=0;j<CAR_COUNT;j++) {
                    Car *c=&g->cars[j]; if (!(nearby & (1u<<j))) continue;
                    for (int end=-1;end<=1;end+=2) {
                        Vec3 center=vadd(c->pos,v3(sinf(c->yaw)*end*.72f,.65f,cosf(c->yaw)*end*.72f));
                        Vec3 d=vsub(node->pos,center); float distance=vlen(d);
                        if (distance>=.9f || distance<.00001f) continue;
                        Vec3 normal=vmul(d,1/distance);
                        node->pos=vadd(center,vmul(normal,.9f));
                        if (iteration!=0) continue;
                        float speed=fmaxf(0,vdot(c->velocity,normal));
                        float kick=fminf(speed*.16f,2.f)*h*8;
                        // Lightweight fabric transfers a small reaction to the car and
                        // bends both stakes around their feet without launching them.
                        c->velocity=vsub(c->velocity,vmul(normal,kick/180));
                        for (int k=0;k<2;k++) {
                            TrackProp *p=&g->props[f->posts[k]];
                            if (speed>.3f) { p->asleep=false; p->quiet_time=0; }
                            impulse(p,v3(0,.4f,0),vmul(normal,kick*.5f));
                        }
                    }
                }
                float ground=track_height(t,node->pos,NULL)+.025f;
                if (node->pos.y<ground) {
                    node->pos.y=ground;
                    node->previous=vlerp(node->previous,node->pos,.25f);
                }
            }
        }
    }
}
