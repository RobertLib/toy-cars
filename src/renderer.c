#include "renderer.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float p[3], n[3], c[3], paint;
} Vertex;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define GLSL "#version 300 es\nprecision highp float;\n"
#else
#define GLSL "#version 330 core\n"
#endif
static const char *world_vs =
    GLSL "layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;layout(location=2) in "
         "vec3 aColor;layout(location=3) in float aPaint;\n"
         "uniform mat4 uModel,uVP,uLight;uniform vec3 uPaint;out vec3 vPos,vNormal,vColor;out vec4 "
         "vShadow;\n"
         "void main(){vec4 "
         "p=uModel*vec4(aPos,1.);vPos=p.xyz;vNormal=mat3(uModel)*aNormal;vColor=mix(aColor,uPaint,"
         "aPaint);vShadow=uLight*p;gl_Position=uVP*p;}";
static const char *world_fs = GLSL
    "in vec3 vPos,vNormal,vColor;in vec4 vShadow;out vec4 frag;uniform sampler2D uShadow;uniform "
    "vec3 uEye,uFog;uniform float uFogFar,uUnlit;\n"
    "float shadow(vec3 n){vec3 "
    "p=vShadow.xyz/vShadow.w*.5+.5;if(p.x<0.||p.x>1.||p.y<0.||p.y>1.||p.z>1.)return 1.;float "
    "bias=max(.0005,.0015*(1.-dot(n,normalize(vec3(-.6,1.,.45)))));float s=0.;for(int "
    "x=-1;x<=1;x++)for(int y=-1;y<=1;y++){float "
    "d=texture(uShadow,p.xy+vec2(x,y)/2048.).r;s+=p.z-bias<=d?1.:0.;}return s/9.;}\n"
    "void main(){vec3 n=normalize(vNormal);if(!gl_FrontFacing)n=-n;vec3 "
    "light=normalize(vec3(-.6,1.,.45));float diff=max(0.,dot(n,light));float sh=shadow(n);float "
    "hemi=n.y*.5+.5;vec3 ambient=mix(vec3(.33,.38,.43),vec3(.71,.75,.74),hemi);vec3 "
    "col=vColor*(ambient+vec3(.39,.34,.27)*diff*sh);vec3 "
    "halfV=normalize(light+normalize(uEye-vPos));float "
    "spec=pow(max(0.,dot(n,halfV)),48.)*.12*sh;col+=spec;col=mix(col,vColor,uUnlit);float "
    "fog=smoothstep(uFogFar*.45,uFogFar,length(uEye-vPos));col=mix(col,uFog,fog*.88);frag=vec4(col,"
    "1.);}";
static const char *depth_vs = GLSL "layout(location=0)in vec3 aPos;uniform mat4 uModel,uVP;void "
                                   "main(){gl_Position=uVP*uModel*vec4(aPos,1.);}";
static const char *depth_fs = GLSL "void main(){}";
static const char *post_vs =
    GLSL "out vec2 uv;void main(){vec2 "
         "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}";
static const char *post_fs = GLSL
    "in vec2 uv;out vec4 frag;uniform sampler2D uScene;uniform vec2 uResolution;uniform float "
    "uVignette;\n"
    "void main(){vec2 px=1./uResolution;vec3 c=texture(uScene,uv).rgb;vec3 "
    "n=texture(uScene,uv+vec2(0,px.y)).rgb,s=texture(uScene,uv-vec2(0,px.y)).rgb,e=texture(uScene,"
    "uv+vec2(px.x,0)).rgb,w=texture(uScene,uv-vec2(px.x,0)).rgb;vec3 l=vec3(.299,.587,.114);float "
    "hi=max(max(dot(n,l),dot(s,l)),max(dot(e,l),dot(w,l))),lo=min(min(dot(n,l),dot(s,l)),min(dot(e,"
    "l),dot(w,l)));float edge=smoothstep(.055,.22,hi-lo);c=mix(c,(n+s+e+w+c*2.)/6.,edge*.60);vec2 "
    "q=uv*(1.-uv);float "
    "vign=clamp(pow(16.*q.x*q.y,.22),.65,1.);c*=mix(1.,vign,uVignette);frag=vec4(c,1.);}";

GLuint render_program(const char *vs, const char *fs) {
    GLuint shaders[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
    const char *source[2] = {vs, fs};
    char log[4096];
    for (int i = 0; i < 2; i++) {
        glShaderSource(shaders[i], 1, &source[i], NULL);
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
static Mesh mesh_upload(const Vertex *data, int count) {
    Mesh m = {.count = count};
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(count * sizeof(Vertex)), data, GL_STATIC_DRAW);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, i == 3 ? 1 : 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void *)(uintptr_t)(i * 3 * sizeof(float)));
    }
    glBindVertexArray(0);
    return m;
}
static bool mesh_load(Mesh *m, const char *path) {
    size_t size;
    unsigned char *d = asset_read(path, &size);
    if (!d || size < 8 || memcmp(d, "TCM1", 4)) {
        SDL_Log("Cannot load mesh %s", path);
        SDL_free(d);
        return false;
    }
    uint32_t count;
    memcpy(&count, d + 4, 4);
    count = SDL_Swap32LE(count);
    if (count > 5000000 || count % 3 || size != 8 + (size_t)count * sizeof(Vertex)) {
        SDL_free(d);
        return false;
    }
    Vertex *data = (Vertex *)(void *)(d + 8);
    // The format is explicitly little endian, including IEEE754 components.
    for (size_t i = 0; i < (size_t)count * 10; i++) {
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
    *m = mesh_upload(data, (int)count);
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
}

bool renderer_init(Renderer *r) {
    memset(r, 0, sizeof *r);
    r->last_track = -1;
    r->shader = render_program(world_vs, world_fs);
    r->depth_shader = render_program(depth_vs, depth_fs);
    r->post_shader = render_program(post_vs, post_fs);
    if (!r->shader || !r->depth_shader || !r->post_shader)
        return false;
    const char *names[] = {"tracks/country.tcm", "tracks/beach.tcm", "tracks/winter.tcm"};
    for (int i = 0; i < 3; i++)
        if (!mesh_load(&r->tracks[i], names[i]))
            return false;
    if (!mesh_load(&r->car, "models/car.tcm") || !mesh_load(&r->fuel, "models/fuel.tcm"))
        return false;
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
    glBindFramebuffer(GL_FRAMEBUFFER, r->default_fbo);
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
            pos = vlerp(c->prev, c->pos, r->alpha);
            yaw = c->prev_yaw + angle_delta(c->yaw, c->prev_yaw) * r->alpha;
            pitch = c->pitch;
            roll = c->roll;
            color = c->color;
        }
        // The modeled tire bottoms are .04 below the car origin.
        Mat4 model = mmul(translate(vadd(pos, v3(0, .04f, 0))),
                          mmul(rotate_y(yaw), mmul(rotate_x(pitch), rotate_z(roll))));
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
            if (p->life <= 0)
                continue;
            float s = p->size * (.3f + .7f * p->life / p->max_life);
            Mat4 model = mmul(translate(p->pos), mmul(rotate_y(p->life * 2), scale3(s, s, s)));
            draw_mesh(r, r->cube, model, p->color, false);
        }
        glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    }
}

void renderer_draw(Renderer *r, const Game *g, float dt, float alpha) {
    bool menu =
        g->screen == SCREEN_MENU || g->screen == SCREEN_HELP || g->screen == SCREEN_SETTINGS;
    bool garage = g->screen == SCREEN_GARAGE;
    bool overview = menu || garage;
    const Track *t = &g->tracks[g->selected];
    r->alpha = alpha;
    r->draw_calls = 0;
    r->triangles = 0;
    r->bound_vao = 0;
    Color clear = overview ? rgb(.956f, .949f, .922f) : t->sky;
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
    uniform_vec(r->shader, "uFog", v3(clear.r, clear.g, clear.b));
    glUniform1f(glGetUniformLocation(r->shader, "uFogFar"), overview ? 1400 : 190);
    glUniform1f(glGetUniformLocation(r->shader, "uUnlit"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->shadow_texture);
    glUniform1i(glGetUniformLocation(r->shader, "uShadow"), 0);
    render_objects(r, g, false, menu, garage);
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
    for (int i = 0; i < 3; i++)
        mesh_delete(&r->tracks[i]);
    mesh_delete(&r->car);
    mesh_delete(&r->fuel);
    mesh_delete(&r->plane);
    mesh_delete(&r->cube);
    glDeleteProgram(r->shader);
    glDeleteProgram(r->depth_shader);
    glDeleteProgram(r->post_shader);
    glDeleteVertexArrays(1, &r->empty_vao);
    glDeleteTextures(1, &r->shadow_texture);
    glDeleteTextures(1, &r->scene_texture);
    glDeleteFramebuffers(1, &r->shadow_fbo);
    glDeleteFramebuffers(1, &r->scene_fbo);
    glDeleteRenderbuffers(1, &r->scene_depth);
}
